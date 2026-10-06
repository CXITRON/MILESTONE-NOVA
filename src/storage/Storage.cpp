#include "Storage.h"
#include "../core/Text.h"
#include "../logging/Log.h"
#include <algorithm>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <new>
namespace nova {
namespace {
bool line(File &f, char *out, size_t cap) {
  size_t n = 0;
  bool overflow = false;
  while (f.available()) {
    int c = f.read();
    if (c < 0 || c == '\n')
      break;
    if (c == '\r')
      continue;
    if (n + 1 < cap)
      out[n++] = c;
    else
      overflow = true;
  }
  out[n] = 0;
  return !overflow;
}
} // namespace
void Storage::importConfig(Settings &s, Secrets &secrets, bool importSettings) {
  bool wifiProvided = false;
  for (const char *path : {"/config/device.ini", "/config/secrets.ini"}) {
    if (!importSettings && !strcmp(path, "/config/device.ini"))
      continue;
    File file = SD.open(path, FILE_READ);
    if (!file || file.size() > 8192)
      continue;
    char text[1024];
    while (file.available()) {
      if (!line(file, text, sizeof(text)) || !text[0] || text[0] == '#')
        continue;
      char *value = strchr(text, '=');
      if (!value)
        continue;
      *value++ = 0;
      if (!strcmp(path, "/config/device.ini")) {
        if (!applySetting(s, text, value))
          log("SETTINGS", "ignored invalid key: %.40s", text);
      } else {
        char *dst = nullptr;
        size_t cap = 0;
        if (!strcmp(text, "ap_password")) {
          dst = secrets.apPassword;
          cap = sizeof(secrets.apPassword);
        } else if (!strcmp(text, "ssid")) {
          dst = secrets.ssid;
          cap = sizeof(secrets.ssid);
          wifiProvided = true;
        } else if (!strcmp(text, "password")) {
          dst = secrets.password;
          cap = sizeof(secrets.password);
        }
        if (dst && strlen(value) < cap)
          strcpy(dst, value);
      }
    }
  }
  if (wifiProvided) {
    WifiProfile profile;
    strcpy(profile.ssid, secrets.ssid);
    strcpy(profile.password, secrets.password);
    rememberWifiProfile(secrets, profile);
  }
}

bool Storage::begin(Settings &s, Secrets &secrets, bool importSettings) {
  pinMode(board::sdCs, OUTPUT);
  digitalWrite(board::sdCs, HIGH);
  spi_.begin(board::sdSck, board::sdMiso, board::sdMosi, board::sdCs);
  mounted_ = SD.begin(board::sdCs, spi_, board::sdHz);
  if (!mounted_)
    log("SD", "unavailable; worker allows later remount");
  if (mounted_) {
    for (const char *dir : {"/config", "/lyrics", "/artwork", "/media", "/logs", "/update"})
      SD.mkdir(dir);
    importConfig(s, secrets, importSettings);
  }
  const auto allocate = [](size_t n) {
    return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  };
  if (auto *p = allocate(sizeof(AssetResult)))
    buffer_ = new (p) AssetResult{};
  if (auto *p = allocate(sizeof(MediaCatalog)))
    catalog_ = new (p) MediaCatalog{};
  if (auto *p = allocate(sizeof(MediaCatalog)))
    published_ = new (p) MediaCatalog{};
  requests_ = xQueueCreate(1, sizeof(AssetRequest));
  results_ = xQueueCreate(1, sizeof(AssetResult *));
  returned_ = xQueueCreate(1, sizeof(uint8_t));
  jobs_ = xQueueCreate(1, sizeof(FileJob *));
  callMutex_ = xSemaphoreCreateMutex();
  done_ = xSemaphoreCreateBinary();
  catalogMutex_ = xSemaphoreCreateMutex();
  if (!buffer_ || !catalog_ || !published_ || !requests_ || !results_ || !returned_ || !jobs_ ||
      !callMutex_ || !done_ || !catalogMutex_ || !decoder_.begin()) {
    mounted_ = false;
    SD.end();
    log("SD", "allocation failed; degraded");
    return false;
  }
  stopped_ = false;
  if (xTaskCreate(task, "nova-sd", 10240, this, 1, nullptr) != pdPASS) {
    mounted_ = false;
    stopped_ = true;
    SD.end();
    return false;
  }
  log("SD", "mounted; indexed media worker started");
  return true;
}
bool Storage::request(const char *key, uint32_t generation) {
  if (!mounted_ || stopping_ || !validTrackKey(key))
    return false;
  AssetRequest r;
  r.generation = generation;
  strcpy(r.name, key);
  return xQueueOverwrite(requests_, &r) == pdTRUE;
}
bool Storage::requestFile(const char *path, uint32_t pos, uint32_t gen) {
  if (!mounted_ || stopping_ || !safeStoragePath(path))
    return false;
  AssetRequest r;
  r.kind = AssetKind::Media;
  r.position = pos;
  r.generation = gen;
  strcpy(r.name, path);
  return xQueueOverwrite(requests_, &r) == pdTRUE;
}
bool Storage::requestMedia(unsigned item, uint32_t pos, uint32_t gen) {
  char path[128]{};
  if (!catalogMutex_ || xSemaphoreTake(catalogMutex_, 0) != pdTRUE)
    return false;
  if (item < published_->count)
    strcpy(path, published_->entries[item].path);
  xSemaphoreGive(catalogMutex_);
  return path[0] && requestFile(path, pos, gen);
}
bool Storage::copyCatalog(MediaCatalog &out) {
  if (!published_ || !catalogMutex_ || xSemaphoreTake(catalogMutex_, 0) != pdTRUE)
    return false;
  out = *published_;
  xSemaphoreGive(catalogMutex_);
  return true;
}
AssetResult *Storage::receive() {
  AssetResult *r = nullptr;
  if (results_)
    xQueueReceive(results_, &r, 0);
  return r;
}
void Storage::release() {
  const uint8_t yes = 1;
  if (returned_)
    xQueueOverwrite(returned_, &yes);
}
void Storage::stop() {
  if (!requests_ || stopped_)
    return;
  portENTER_CRITICAL(&jobGate_);
  stopping_ = true;
  validationCancelled_ = true;
  portEXIT_CRITICAL(&jobGate_);
  AssetRequest r;
  r.kind = AssetKind::Stop;
  xQueueOverwrite(requests_, &r);
}
bool Storage::readExact(File &file, uint8_t *data, size_t length) {
  while (length && !stopping_) {
    const size_t n = std::min(length, size_t(4096));
    if (file.read(data, n) != n)
      return false;
    data += n;
    length -= n;
    vTaskDelay(1);
  }
  return length == 0;
}
void Storage::readTrack(AssetResult &r) {
  char path[128];
  if (!LyricsCache::path(path, sizeof(path), r.request.name)) {
    r.error = true;
    return;
  }
  File lrc = SD.open(path, FILE_READ);
  if (lrc) {
    const size_t n = lrc.size();
    if (n <= maxLyricsBytes && readExact(lrc, reinterpret_cast<uint8_t *>(r.lyrics), n)) {
      r.lyricsBytes = n;
      r.lyrics[n] = 0;
      r.lyricsPresent = true;
    } else
      r.error = true;
    lrc.close();
  }
  snprintf(path, sizeof(path), "/artwork/%.16s.block", r.request.name);
  r.blocked = SD.exists(path);
  snprintf(path, sizeof(path), "/artwork/%.16s.missing", r.request.name);
  r.blocked = r.blocked || SD.exists(path);
  for (const char *suffix : {"pin", "custom"}) {
    snprintf(path, sizeof(path), "/artwork/%.16s.nvi.%s", r.request.name, suffix);
    r.blocked = r.blocked || SD.exists(path);
  }
  // Blocking automatic lookup does not hide a successfully cached image.
  snprintf(path, sizeof(path), "/artwork/%.16s.nvi", r.request.name);
  if (SD.exists(path)) {
    uint32_t duration = 0;
    r.artPresent = decoder_.frame(path, 0, r.pixels, duration, board::artSide);
    if (!r.artPresent) {
      char backup[144];
      snprintf(backup, sizeof(backup), "%s.bak", path);
      if (SD.exists(backup))
        r.artPresent = decoder_.frame(backup, 0, r.pixels, duration, board::artSide);
    }
    r.error = !r.artPresent;
    if (r.artPresent) {
      char used[144];
      snprintf(used, sizeof(used), "%s.used", path);
      File marker = SD.open(used, FILE_WRITE);
      if (marker) {
        marker.write(uint8_t(1));
        marker.close();
      }
    }
  }
}
void Storage::readMedia(AssetResult &r) {
  const char *path = r.request.name;
  const int64_t started = esp_timer_get_time();
  if (!decoder_.frame(path, r.request.position, r.pixels, r.durationMs, board::mediaSide)) {
    if (decoder_.validate(path, progress_, validationCancelled_))
      r.artPresent =
          decoder_.frame(path, r.request.position, r.pixels, r.durationMs, board::mediaSide);
  } else
    r.artPresent = true;
  r.error = !r.artPresent;
  r.readUs = decoder_.readUs();
  r.jpegUs = decoder_.jpegUs();
  r.totalUs = uint32_t(esp_timer_get_time() - started);
}
bool Storage::execute(FileJob &job) {
  job.ok = false;
  if ((!mounted_ && job.op != FileOp::Scan) || stopping_ || stopped_ || !callMutex_)
    return false;
  if (xSemaphoreTake(callMutex_, pdMS_TO_TICKS(5000)) != pdTRUE) {
    strcpy(job.error, "Storage busy");
    return false;
  }
  FileJob *ptr = &job;
  portENTER_CRITICAL(&jobGate_);
  const bool queued = !stopping_ && xQueueSend(jobs_, &ptr, 0) == pdTRUE;
  portEXIT_CRITICAL(&jobGate_);
  if (!queued) {
    xSemaphoreGive(callMutex_);
    return false;
  }
  xSemaphoreTake(done_, portMAX_DELAY);
  xSemaphoreGive(callMutex_);
  return job.ok;
}
void Storage::task(void *self) { static_cast<Storage *>(self)->run(); }
void Storage::run() {
  if (mounted_) {
    scan();
    recoverTransfer();
    if (!recoverLog())
      log("LOG", "pending journal invalid or I/O failure; retained");
  }
  bool leased = false;
  while (!stopping_) {
    if (leased) {
      uint8_t ack;
      if (xQueueReceive(returned_, &ack, pdMS_TO_TICKS(10)) == pdTRUE)
        leased = false;
      else
        continue;
    }
    FileJob *job = nullptr;
    if (xQueueReceive(jobs_, &job, 0) == pdTRUE) {
      fileJob(*job);
      xSemaphoreGive(done_);
      continue;
    }
    AssetRequest r;
    if (xQueueReceive(requests_, &r, pdMS_TO_TICKS(10)) != pdTRUE)
      continue;
    if (r.kind == AssetKind::Stop)
      break;
    buffer_->request = r;
    buffer_->lyricsPresent = buffer_->artPresent = buffer_->error = buffer_->blocked = false;
    buffer_->lyricsBytes = buffer_->durationMs = buffer_->frame = 0;
    if (r.kind == AssetKind::Track)
      readTrack(*buffer_);
    else
      readMedia(*buffer_);
    if (!stopping_) {
      xQueueSend(results_, &buffer_, portMAX_DELAY);
      leased = true;
    }
  }
  if (leased) {
    uint8_t ack;
    xQueueReceive(returned_, &ack, portMAX_DELAY);
  }
  FileJob *pending = nullptr;
  if (xQueueReceive(jobs_, &pending, 0) == pdTRUE) {
    pending->ok = false;
    strcpy(pending->error, "Storage stopping");
    xSemaphoreGive(done_);
  }
  uploadFile_.flush();
  uploadFile_.close();
  decoder_.close();
  SD.end();
  spi_.end();
  stopped_ = true;
  vTaskDelete(nullptr);
}
} // namespace nova

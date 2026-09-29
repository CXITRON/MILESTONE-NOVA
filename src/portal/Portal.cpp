#include "Portal.h"
#include "../core/Text.h"
#include "Assets.h"
#include <WiFi.h>
#include <algorithm>
#include <cJSON.h>
#include <cmath>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <memory>
#include <new>
namespace nova {
namespace {
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
Json parse(const String &body) {
  return Json(body.length() <= 8192 ? cJSON_Parse(body.c_str()) : nullptr, cJSON_Delete);
}
const char *str(cJSON *root, const char *key) {
  auto *v = cJSON_GetObjectItemCaseSensitive(root, key);
  return cJSON_IsString(v) ? v->valuestring : "";
}
bool num(cJSON *root, const char *key, uint32_t &out, uint32_t limit = UINT32_MAX) {
  auto *v = cJSON_GetObjectItemCaseSensitive(root, key);
  if (!cJSON_IsNumber(v) || !std::isfinite(v->valuedouble) || v->valuedouble < 0 ||
      v->valuedouble > limit || floor(v->valuedouble) != v->valuedouble)
    return false;
  out = v->valuedouble;
  return true;
}
bool yes(cJSON *root, const char *key) {
  return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, key));
}
bool copy(char *to, size_t capacity, const char *from) {
  if (strlen(from) >= capacity || !validUtf8(from))
    return false;
  strcpy(to, from);
  return true;
}
bool publicPath(const char *path) {
  return safeStoragePath(path) && (!strncmp(path, "/media/", 7) || !strncmp(path, "/artwork/", 9) ||
                                   !strncmp(path, "/lyrics/", 8) || !strncmp(path, "/logs/", 6));
}
bool decimal(const String &text, uint32_t &out) {
  if (text.isEmpty())
    return false;
  uint64_t n = 0;
  for (char c : text) {
    if (c < '0' || c > '9')
      return false;
    n = n * 10 + c - '0';
    if (n > UINT32_MAX)
      return false;
  }
  out = n;
  return true;
}
} // namespace
bool Portal::begin(Storage &s, Artwork &a, Firmware &f) {
  storage_ = &s;
  artwork_ = &a;
  firmware_ = &f;
  const auto alloc = [](size_t n) {
    return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  };
  if (auto *p = alloc(sizeof(PortalSnapshot)))
    published_ = new (p) PortalSnapshot{};
  if (auto *p = alloc(sizeof(PortalSnapshot)))
    view_ = new (p) PortalSnapshot{};
  if (auto *p = alloc(sizeof(MediaCatalog)))
    catalog_ = new (p) MediaCatalog{};
  bytes_ = static_cast<uint8_t *>(alloc(262145));
  commands_ = xQueueCreate(1, sizeof(PortalCommand *));
  logs_ = xQueueCreate(8, sizeof(LogLine));
  done_ = xSemaphoreCreateBinary();
  mutex_ = xSemaphoreCreateMutex();
  ready_ = published_ && view_ && catalog_ && bytes_ && commands_ && logs_ && done_ && mutex_ &&
           xTaskCreate(task, "nova-service", 12288, this, 1, nullptr) == pdPASS;
  return ready_;
}
void Portal::open(bool enabled) {
  if (!enabled && requested_)
    cleanupSync_ = true;
  requested_ = ready_ && enabled && !suspendRequested_;
}
void Portal::suspend(bool enabled) {
  if (enabled)
    open(false);
  else
    suspended_ = false;
  suspendRequested_ = enabled;
}
void Portal::publish(const PortalSnapshot &s) {
  if (mutex_ && published_ && xSemaphoreTake(mutex_, 0) == pdTRUE) {
    *published_ = s;
    xSemaphoreGive(mutex_);
  }
}
void Portal::snapshot() {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  *view_ = *published_;
  xSemaphoreGive(mutex_);
}
PortalCommand *Portal::receive() {
  PortalCommand *c = nullptr;
  if (commands_)
    xQueueReceive(commands_, &c, 0);
  return c;
}
void Portal::reply(PortalCommand &) { xSemaphoreGive(done_); }
bool Portal::dispatch(PortalCommand &c) {
  c.ok = false;
  c.error[0] = 0;
  PortalCommand *ptr = &c;
  if (xQueueSend(commands_, &ptr, 0) != pdTRUE)
    return false;
  // Main loop services this mailbox even while closing AP or shutting down.
  xSemaphoreTake(done_, portMAX_DELAY);
  return c.ok;
}
bool Portal::logEnvironment(const char *path, const char *line) {
  LogLine entry;
  return ready_ && !suspendRequested_ && logs_ && copy(entry.path, sizeof(entry.path), path) &&
         copy(entry.line, sizeof(entry.line), line) && xQueueSend(logs_, &entry, 0) == pdTRUE;
}
void Portal::json(void *value, int code) {
  auto *root = static_cast<cJSON *>(value);
  char *encoded = cJSON_PrintUnformatted(root);
  server_.sendHeader("Cache-Control", "no-store");
  server_.send(code, "application/json; charset=utf-8",
               encoded ? encoded : "{\"ok\":false,\"message\":\"Out of memory\"}");
  cJSON_free(encoded);
  cJSON_Delete(root);
}
void Portal::respond(bool ok, const char *message) {
  auto *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", ok);
  cJSON_AddStringToObject(root, "message", message);
  json(root, ok ? 200 : 400);
}
bool Portal::authorized() {
  const String host = server_.hostHeader();
  const bool localHost =
      host == "192.168.4.1" || host == "192.168.4.1:80" || host == "milestone-nova.local";
  // The token is scoped to this physical AP session; no CORS or remote-origin mutation.
  return requested_ && server_.client().localIP() == WiFi.softAPIP() && localHost &&
         server_.header("X-NOVA") == token_;
}
void Portal::status() {
  snapshot();
  const auto &v = *view_;
  auto *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "token", token_);
  cJSON_AddNumberToObject(root, "mode", v.mode);
  cJSON_AddStringToObject(root, "message", v.message);
  cJSON_AddStringToObject(root, "ble", v.ble);
  cJSON_AddBoolToObject(root, "sd", v.sd);
  cJSON_AddNumberToObject(root, "uptime", v.uptime);
  cJSON_AddNumberToObject(root, "heap", v.heap);
  cJSON_AddNumberToObject(root, "psram", v.psram);
  cJSON_AddNumberToObject(root, "temperature", v.temperature);
  cJSON_AddNumberToObject(root, "humidity", v.humidity);
  cJSON_AddBoolToObject(root, "sensor", v.sensor);
  cJSON_AddNumberToObject(root, "volts", v.volts);
  cJSON_AddNumberToObject(root, "syncSession", v.syncSession);
  cJSON_AddBoolToObject(root, "syncStale", v.syncStale);
  cJSON_AddNumberToObject(root, "position", v.position);
  cJSON_AddBoolToObject(root, "playing", v.playing);
  auto *track = cJSON_AddObjectToObject(root, "track");
  cJSON_AddStringToObject(track, "key", v.track.key);
  cJSON_AddStringToObject(track, "title", v.track.title);
  cJSON_AddStringToObject(track, "artist", v.track.artist);
  cJSON_AddStringToObject(track, "album", v.track.album);
  auto *net = cJSON_AddObjectToObject(root, "network");
  cJSON_AddStringToObject(net, "status", v.network.status);
  cJSON_AddStringToObject(net, "ip", v.network.ip);
  cJSON_AddStringToObject(net, "test", v.network.test);
  cJSON_AddBoolToObject(net, "connected", v.network.connected);
  cJSON_AddBoolToObject(net, "scanning", v.network.scanning);
  auto *scan = cJSON_AddArrayToObject(net, "scan");
  for (unsigned i = 0; i < v.network.scanCount; ++i) {
    const auto &s = v.network.scan[i];
    auto *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "ssid", s.ssid);
    cJSON_AddNumberToObject(item, "rssi", s.rssi);
    cJSON_AddNumberToObject(item, "auth", s.auth);
    cJSON_AddBoolToObject(item, "supported", s.supported);
    cJSON_AddItemToArray(scan, item);
  }
  auto *saved = cJSON_AddArrayToObject(net, "saved");
  for (unsigned i = 0; i < v.wifiCount; ++i)
    cJSON_AddItemToArray(saved, cJSON_CreateString(v.wifiSaved[i]));
  cJSON_AddStringToObject(root, "diagnostics", v.diagnostics);
  cJSON_AddNumberToObject(root, "storageProgress", storage_->progress());
  json(root);
}
void Portal::settings() {
  snapshot();
  if (server_.method() == HTTP_GET) {
    auto *root = cJSON_CreateObject();
    auto *values = cJSON_AddObjectToObject(root, "values");
    auto *specs = cJSON_AddArrayToObject(root, "specs");
    char value[256];
    for (unsigned i = 0; i < settingCount(); ++i) {
      const auto &s = settingSpec(i);
      settingValue(view_->settings, i, value, sizeof(value));
      cJSON_AddStringToObject(values, s.name, value);
      auto *item = cJSON_CreateObject();
      cJSON_AddStringToObject(item, "name", s.name);
      cJSON_AddNumberToObject(item, "type", unsigned(s.type));
      cJSON_AddNumberToObject(item, "min", s.minimum);
      cJSON_AddNumberToObject(item, "max", s.maximum);
      cJSON_AddItemToArray(specs, item);
    }
    snprintf(value, sizeof(value), "%04u-%02u-%02u", view_->settings.ddayYear,
             view_->settings.ddayMonth, view_->settings.ddayDay);
    cJSON_AddStringToObject(values, "dday", value);
    for (unsigned i = 0; i < 9; ++i) {
      value[i * 2] = '0' + view_->settings.coreOrder[i];
      value[i * 2 + 1] = i == 8 ? 0 : ',';
    }
    cJSON_AddStringToObject(values, "core_order", value);
    json(root);
    return;
  }
  if (!authorized()) {
    respond(false, "AP session expired");
    return;
  }
  auto root = parse(body());
  if (!cJSON_IsObject(root.get())) {
    respond(false, "Invalid settings");
    return;
  }
  command_ = PortalCommand{};
  command_.kind = CommandKind::Settings;
  command_.settings = view_->settings;
  cJSON *item = nullptr;
  cJSON_ArrayForEach(item, root.get()) {
    if (!cJSON_IsString(item) || !item->string || !strcmp(item->string, "ap_mode") ||
        !applySetting(command_.settings, item->string, item->valuestring)) {
      respond(false, "Invalid setting or dependent range");
      return;
    }
  }
  respond(dispatch(command_), command_.error);
}
void Portal::action() {
  if (!authorized()) {
    respond(false, "AP session expired");
    return;
  }
  auto root = parse(body());
  if (!root) {
    respond(false, "Invalid JSON");
    return;
  }
  command_ = PortalCommand{};
  const char *op = str(root.get(), "op");
  bool valid = true;
  if (!strcmp(op, "mode")) {
    command_.kind = CommandKind::Mode;
    valid = num(root.get(), "value", command_.id, 2);
  } else if (!strcmp(op, "scan"))
    command_.kind = CommandKind::WifiScan;
  else if (!strcmp(op, "wifi")) {
    command_.kind = CommandKind::WifiTest;
    valid =
        copy(command_.wifi.ssid, sizeof(command_.wifi.ssid), str(root.get(), "ssid")) &&
        copy(command_.wifi.password, sizeof(command_.wifi.password), str(root.get(), "password")) &&
        copy(command_.wifi.identity, sizeof(command_.wifi.identity), str(root.get(), "identity")) &&
        copy(command_.wifi.username, sizeof(command_.wifi.username), str(root.get(), "username")) &&
        num(root.get(), "auth", command_.id, 1);
    command_.wifi.auth = command_.id;
    valid = valid && validWifiProfile(command_.wifi);
  } else if (!strcmp(op, "wifiDelete") || !strcmp(op, "wifiUse")) {
    command_.kind = !strcmp(op, "wifiDelete") ? CommandKind::WifiDelete : CommandKind::WifiUse;
    valid = num(root.get(), "value", command_.id, 7);
  } else if (!strcmp(op, "ap")) {
    command_.kind = CommandKind::ApPassword;
    valid = num(root.get(), "mode", command_.id, 2) &&
            copy(command_.text, sizeof(command_.text), str(root.get(), "password"));
    if (command_.id == 2)
      valid = valid && yes(root.get(), "confirmed");
  } else if (!strcmp(op, "apClose")) {
    command_.kind = CommandKind::ApClose;
  } else if (!strcmp(op, "time")) {
    command_.kind = CommandKind::Time;
    auto *epoch = cJSON_GetObjectItemCaseSensitive(root.get(), "epoch");
    valid = cJSON_IsNumber(epoch) && std::isfinite(epoch->valuedouble) &&
            epoch->valuedouble >= 946684800 && epoch->valuedouble <= 7258118399LL &&
            floor(epoch->valuedouble) == epoch->valuedouble;
    if (valid)
      command_.epoch = epoch->valuedouble;
  } else if (!strcmp(op, "legacyImport"))
    command_.kind = CommandKind::LegacyImport;
  else if (!strcmp(op, "ntp"))
    command_.kind = CommandKind::Ntp;
  else if (!strcmp(op, "sensor"))
    command_.kind = CommandKind::SensorScan;
  else if (!strcmp(op, "defaults") || !strcmp(op, "factory")) {
    command_.kind = !strcmp(op, "defaults") ? CommandKind::Defaults : CommandKind::FactoryReset;
    valid = yes(root.get(), "confirmed");
  } else if (!strcmp(op, "syncStart")) {
    command_.kind = CommandKind::SyncStart;
    valid = num(root.get(), "session", command_.id) && command_.id;
    FileJob check;
    check.op = FileOp::SyncValidate;
    strcpy(check.path, "/media/sync.njv");
    valid = valid && num(root.get(), "size", check.offset, 0x7fffffff) && check.offset &&
            num(root.get(), "crc", check.checksum);
    if (valid && !storage_->execute(check)) {
      respond(false, check.error);
      return;
    }
    command_.duration = check.total;
  } else if (!strcmp(op, "sync")) {
    command_.kind = CommandKind::SyncTick;
    valid = num(root.get(), "session", command_.id) &&
            num(root.get(), "sequence", command_.sequence) &&
            num(root.get(), "position", command_.position, 21600000);
    command_.playing = yes(root.get(), "playing");
  } else if (!strcmp(op, "syncStop")) {
    command_.kind = CommandKind::SyncStop;
    valid = num(root.get(), "session", command_.id) && command_.id;
  } else if (!strcmp(op, "artRefresh"))
    command_.kind = CommandKind::ArtRefresh;
  else if (!strcmp(op, "ota"))
    command_.kind = CommandKind::OtaWindow;
  else if (!strcmp(op, "updateCheck"))
    command_.kind = CommandKind::UpdateCheck;
  else if (!strcmp(op, "updateDownload"))
    command_.kind = CommandKind::UpdateDownload;
  else if (!strcmp(op, "updatePrepare"))
    command_.kind = CommandKind::UpdatePrepare;
  else if (!strcmp(op, "logsClear")) {
    command_.kind = CommandKind::LogsClear;
    valid = yes(root.get(), "confirmed");
  } else if (!strcmp(op, "restart")) {
    command_.kind = CommandKind::Restart;
    valid = yes(root.get(), "confirmed");
  } else
    valid = false;
  if (!valid) {
    respond(false, "Invalid action or data");
    return;
  }
  respond(dispatch(command_), command_.error);
}
void Portal::media() {
  if (server_.method() == HTTP_GET) {
    if (!storage_->copyCatalog(*catalog_)) {
      respond(false, "Catalog busy");
      return;
    }
    auto *root = cJSON_CreateArray();
    for (unsigned i = 0; i < catalog_->count; ++i) {
      const auto &e = catalog_->entries[i];
      auto *item = cJSON_CreateObject();
      cJSON_AddNumberToObject(item, "id", i);
      cJSON_AddStringToObject(item, "path", e.path);
      cJSON_AddStringToObject(item, "title", e.title);
      cJSON_AddNumberToObject(item, "seconds", e.seconds);
      cJSON_AddNumberToObject(item, "bytes", e.bytes);
      cJSON_AddBoolToObject(item, "enabled", e.enabled);
      cJSON_AddItemToArray(root, item);
    }
    json(root);
    return;
  }
  if (!authorized()) {
    respond(false, "AP session expired");
    return;
  }
  auto root = parse(body());
  if (!root) {
    respond(false, "Invalid JSON");
    return;
  }
  const char *op = str(root.get(), "op");
  FileJob job;
  bool valid = true;
  if (!strcmp(op, "repair"))
    job.op = FileOp::Scan;
  else if (!strcmp(op, "clear")) {
    job.op = FileOp::MediaClear;
    valid = yes(root.get(), "confirmed");
  } else if (!strcmp(op, "delete")) {
    job.op = FileOp::MediaDelete;
    valid = num(root.get(), "id", job.id, 63);
  } else if (!strcmp(op, "edit")) {
    job.op = FileOp::MediaEdit;
    valid = num(root.get(), "id", job.id, 63) && num(root.get(), "order", job.offset, 63) &&
            num(root.get(), "seconds", job.value, 3600) &&
            copy(job.text, sizeof(job.text), str(root.get(), "title"));
    job.enabled = yes(root.get(), "enabled");
  } else
    valid = false;
  respond(valid && storage_->execute(job), job.error);
}
void Portal::files() {
  const String dir = server_.arg("dir");
  if (dir != "/artwork" && dir != "/lyrics" && dir != "/logs") {
    respond(false, "Invalid directory");
    return;
  }
  FileJob job;
  job.op = FileOp::List;
  strcpy(job.path, dir.c_str());
  job.data = bytes_;
  job.length = sizeof(FileListing);
  if (server_.hasArg("offset") && !decimal(server_.arg("offset"), job.offset)) {
    respond(false, "Invalid offset");
    return;
  }
  if (!copy(job.text, sizeof(job.text), server_.arg("q").c_str())) {
    respond(false, "Query too long");
    return;
  }
  if (!storage_->execute(job)) {
    respond(false, job.error);
    return;
  }
  const auto &list = *reinterpret_cast<const FileListing *>(bytes_);
  auto *root = cJSON_CreateArray();
  for (unsigned i = 0; i < list.count; ++i) {
    const auto &f = list.entries[i];
    auto *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "path", f.path);
    cJSON_AddNumberToObject(item, "bytes", f.bytes);
    cJSON_AddBoolToObject(item, "pinned", f.pinned);
    cJSON_AddBoolToObject(item, "custom", f.custom);
    cJSON_AddBoolToObject(item, "blocked", f.blocked);
    cJSON_AddBoolToObject(item, "missing", f.missing);
    cJSON_AddStringToObject(item, "text", f.text);
    cJSON_AddItemToArray(root, item);
  }
  json(root);
}
void Portal::file() {
  const String path = server_.arg("path");
  if (!publicPath(path.c_str())) {
    respond(false, "Invalid path");
    return;
  }
  FileJob job;
  job.op = FileOp::Info;
  strcpy(job.path, path.c_str());
  if (!storage_->execute(job)) {
    server_.send(404, "text/plain", "File missing");
    return;
  }
  const uint32_t total = job.total;
  server_.setContentLength(total);
  server_.send(
      200, path.startsWith("/logs/") ? "text/csv; charset=utf-8" : "application/octet-stream", "");
  job.op = FileOp::Read;
  job.data = bytes_;
  for (uint32_t at = 0; at < total && requested_;) {
    job.offset = at;
    job.length = std::min<uint32_t>(16384, total - at);
    if (!storage_->execute(job) || !job.actual)
      break;
    server_.sendContent(reinterpret_cast<char *>(bytes_), job.actual);
    at += job.actual;
    if (!server_.client().connected())
      break;
    vTaskDelay(1);
  }
}
void Portal::transfer() {
  if (!authorized()) {
    respond(false, "AP session expired");
    return;
  }
  auto root = parse(body());
  if (!root) {
    respond(false, "Invalid JSON");
    return;
  }
  FileJob job;
  const char *op = str(root.get(), "op");
  bool valid = true;
  if (!strcmp(op, "status"))
    job.op = FileOp::UploadStatus;
  else if (!strcmp(op, "begin")) {
    job.op = FileOp::UploadBegin;
    valid = num(root.get(), "id", job.id) && num(root.get(), "total", job.total, 0x7FFFFFFF) &&
            copy(job.path, sizeof(job.path), str(root.get(), "path")) &&
            (!strncmp(job.path, "/media/", 7) || !strcmp(job.path, "/update/candidate.bin"));
    job.enabled = yes(root.get(), "replace");
  } else if (!strcmp(op, "commit")) {
    job.op = FileOp::UploadCommit;
    valid = num(root.get(), "id", job.id);
  } else if (!strcmp(op, "abort")) {
    job.op = FileOp::UploadAbort;
    valid = num(root.get(), "id", job.id);
  } else
    valid = false;
  if (!valid || !storage_->execute(job)) {
    respond(false, job.error);
    return;
  }
  auto *out = cJSON_CreateObject();
  cJSON_AddBoolToObject(out, "ok", true);
  cJSON_AddNumberToObject(out, "id", job.id);
  cJSON_AddNumberToObject(out, "offset", job.offset);
  cJSON_AddNumberToObject(out, "total", job.total);
  cJSON_AddStringToObject(out, "path", job.path);
  json(out);
}
String Portal::body() {
  if (!jsonReady_)
    return String();
  jsonReady_ = false;
  return String(reinterpret_cast<char *>(bytes_));
}
void Portal::readJson() {
  if (!server_.header("Content-Type").startsWith("application/json")) {
    jsonReady_ = false;
    server_.client().stop();
    return;
  }
  const auto &raw = server_.raw();
  if (raw.status == RAW_START) {
    received_ = 0;
    jsonReady_ = false;
    if (!authorized())
      server_.client().stop();
  } else if (raw.status == RAW_WRITE) {
    if (raw.currentSize > 8192 - received_) {
      server_.client().stop();
      return;
    }
    memcpy(bytes_ + received_, raw.buf, raw.currentSize);
    received_ += raw.currentSize;
  } else if (raw.status == RAW_END) {
    bytes_[received_] = 0;
    jsonReady_ = received_ > 0;
  } else
    jsonReady_ = false;
}
void Portal::upload() {
  if (!server_.header("Content-Type").startsWith("multipart/")) {
    uploadOk_ = false;
    server_.client().stop();
    return;
  }
  const auto &u = server_.upload();
  if (u.status == UPLOAD_FILE_START) {
    received_ = 0;
    uploadOk_ = authorized();
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (u.currentSize > 262144 - received_) {
      uploadOk_ = false;
      server_.client().stop();
      return;
    }
    if (uploadOk_) {
      memcpy(bytes_ + received_, u.buf, u.currentSize);
      received_ += u.currentSize;
    }
  } else if (u.status == UPLOAD_FILE_ABORTED)
    uploadOk_ = false;
}
void Portal::chunk() {
  if (!authorized() || !uploadOk_ || !received_) {
    respond(false, "Invalid upload");
    return;
  }
  const String kind = server_.arg("kind");
  FileJob job;
  if (kind == "image") {
    MediaInfo info;
    const String key = server_.arg("key");
    snapshot();
    const bool valid =
        validTrackKey(key.c_str()) && mediaHeader(bytes_, received_, received_, info) &&
        info.format == MediaFormat::Image && crc32(bytes_ + 16, 51200) == info.checksum;
    if (valid && key == view_->track.key)
      artwork_->remember(view_->track);
    respond(valid && artwork_->save(key.c_str(), reinterpret_cast<uint16_t *>(bytes_ + 16), true,
                                    view_->settings),
            "Artwork upload");
    return;
  }
  if (kind == "lyrics") {
    const String key = server_.arg("key");
    bytes_[received_] = 0;
    if (!validTrackKey(key.c_str()) || received_ > maxLyricsBytes ||
        strlen(reinterpret_cast<char *>(bytes_)) != received_ ||
        !validUtf8(reinterpret_cast<char *>(bytes_))) {
      respond(false, "Invalid UTF-8 lyrics");
      return;
    }
    job.op = FileOp::Write;
    snprintf(job.path, sizeof(job.path), "/lyrics/%s.lrc", key.c_str());
    job.data = bytes_;
    job.length = received_;
    respond(storage_->execute(job), job.error);
    return;
  }
  job.op = FileOp::UploadChunk;
  job.data = bytes_;
  job.length = received_;
  if (!decimal(server_.arg("id"), job.id) || !decimal(server_.arg("offset"), job.offset) ||
      !decimal(server_.arg("crc"), job.checksum)) {
    respond(false, "Invalid chunk fields");
    return;
  }
  if (!storage_->execute(job)) {
    respond(false, job.error);
    return;
  }
  auto *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", true);
  cJSON_AddNumberToObject(root, "offset", job.offset);
  json(root);
}
void Portal::artwork() {
  if (!authorized()) {
    respond(false, "AP session expired");
    return;
  }
  auto root = parse(body());
  if (!root) {
    respond(false, "Invalid JSON");
    return;
  }
  const char *op = str(root.get(), "op"), *key = str(root.get(), "key");
  FileJob job;
  if (!strcmp(op, "search") || !strcmp(op, "use")) {
    Track query;
    if (!copy(query.title, sizeof(query.title), str(root.get(), "title")) ||
        !copy(query.artist, sizeof(query.artist), str(root.get(), "artist")) ||
        !copy(query.album, sizeof(query.album), str(root.get(), "album"))) {
      respond(false, "Invalid query");
      return;
    }
    auto *pixels = reinterpret_cast<uint16_t *>(bytes_ + 16);
    if (!artwork_->fetch(query, pixels)) {
      respond(false, "Artwork service: no valid result");
      return;
    }
    if (!strcmp(op, "use")) {
      snapshot();
      if (validTrackKey(key)) {
        copy(query.key, sizeof(query.key), key);
        artwork_->remember(query);
      }
      respond(validTrackKey(key) && artwork_->save(key, pixels, true, view_->settings),
              "Artwork saved");
      return;
    }
    memcpy(bytes_, "NVI1", 4);
    write16(bytes_ + 4, 160);
    write16(bytes_ + 6, 160);
    write32(bytes_ + 8, 51200);
    write32(bytes_ + 12, crc32(bytes_ + 16, 51200));
    server_.setContentLength(51216);
    server_.send(200, "application/octet-stream", "");
    server_.sendContent(reinterpret_cast<char *>(bytes_), 51216);
    return;
  }
  if (!validTrackKey(key)) {
    respond(false, "Invalid artwork key");
    return;
  }
  snprintf(job.path, sizeof(job.path), "/artwork/%s.nvi", key);
  if (!strcmp(op, "refresh")) {
    Track track;
    job.op = FileOp::ArtRefresh;
    job.data = reinterpret_cast<uint8_t *>(&track);
    job.length = sizeof(track);
    if (!storage_->execute(job)) {
      respond(false, job.error);
      return;
    }
    auto *pixels = reinterpret_cast<uint16_t *>(bytes_);
    snapshot();
    const bool ok =
        artwork_->fetch(track, pixels) && artwork_->save(key, pixels, false, view_->settings);
    respond(ok, ok ? "Artwork refreshed" : "Artwork service unavailable");
    return;
  }
  if (!strcmp(op, "delete"))
    job.op = FileOp::Remove;
  else if (!strcmp(op, "pin") || !strcmp(op, "block") || !strcmp(op, "custom")) {
    job.op = FileOp::ArtFlags;
    job.value = !strcmp(op, "pin") ? 1 : !strcmp(op, "block") ? 2 : 0;
    job.enabled = yes(root.get(), "enabled");
  } else {
    respond(false, "Invalid artwork action");
    return;
  }
  respond(storage_->execute(job), job.error);
}
void Portal::routes() {
  server_.addMiddleware([this](WebServer &server, Middleware::Callback next) {
    if (!requested_ || server.client().localIP() != WiFi.softAPIP()) {
      server.send(403, "text/plain", "Connect to the device setup AP");
      return true;
    }
    return next();
  });
  const char *headers[]{"X-NOVA", "Content-Type"};
  server_.collectHeaders(headers, 2);
  for (size_t i = 0; i < portalAssetCount; ++i)
    server_.on(portalAssets[i].path, HTTP_GET, [this, i] {
      const auto &a = portalAssets[i];
      server_.sendHeader("Content-Encoding", "gzip");
      server_.sendHeader("Cache-Control", "no-cache");
      server_.send_P(200, a.type, reinterpret_cast<const char *>(a.bytes), a.size);
    });
  server_.on("/api/status", HTTP_GET, [this] { status(); });
  server_.on("/api/settings", HTTP_ANY, [this] { settings(); }, [this] { readJson(); });
  server_.on("/api/action", HTTP_POST, [this] { action(); }, [this] { readJson(); });
  server_.on("/api/media", HTTP_ANY, [this] { media(); }, [this] { readJson(); });
  server_.on("/api/files", HTTP_GET, [this] { files(); });
  server_.on("/api/file", HTTP_GET, [this] { file(); });
  server_.on("/api/transfer", HTTP_POST, [this] { transfer(); }, [this] { readJson(); });
  server_.on("/api/blob", HTTP_POST, [this] { chunk(); }, [this] { upload(); });
  server_.on("/api/artwork", HTTP_POST, [this] { artwork(); }, [this] { readJson(); });
  server_.onNotFound([this] {
    server_.sendHeader("Location", "http://192.168.4.1/");
    server_.send(302, "text/plain", "Open NOVA setup");
  });
}
void Portal::task(void *self) { static_cast<Portal *>(self)->run(); }
void Portal::run() {
  routes();
  bool running = false;
  for (;;) {
    if (requested_ && !running) {
      quiescent_ = false;
      for (unsigned i = 0; i < 4; ++i)
        snprintf(token_ + i * 8, 9, "%08lx", static_cast<unsigned long>(esp_random()));
      dns_.start(53, "*", IPAddress(192, 168, 4, 1));
      server_.begin();
      running = true;
    }
    if (cleanupSync_.exchange(false)) {
      FileJob cleanup;
      cleanup.op = FileOp::Remove;
      strcpy(cleanup.path, "/media/sync.njv");
      storage_->execute(cleanup);
      strcpy(cleanup.path, "/media/sync.njv.nix");
      storage_->execute(cleanup);
    }
    if (!requested_ && running) {
      server_.stop();
      dns_.stop();
      running = false;
      token_[0] = 0;
      quiescent_ = true;
    }
    LogLine entry;
    const bool hadLog = xQueueReceive(logs_, &entry, 0) == pdTRUE;
    if (hadLog) {
      FileJob job;
      job.op = FileOp::Log;
      strcpy(job.path, entry.path);
      job.data = reinterpret_cast<uint8_t *>(entry.line);
      job.length = strlen(entry.line);
      storage_->execute(job);
    }
    if (suspendRequested_) {
      // Ack only after in-flight HTTP/HTTPS work has returned, AP has closed, and logs drained.
      suspended_ = !running && !hadLog;
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    suspended_ = false;
    if (running) {
      dns_.processNextRequest();
      server_.handleClient();
    } else
      artwork_->process();
    firmware_->process();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
} // namespace nova

#include "Firmware.h"
#include "Release.h"
#include "Trust.h"
#include "../core/Text.h"
#include "../network/Endpoints.h"
#include "../network/Http.h"
#include <SHA2Builder.h>
#include <Preferences.h>
#include <Update.h>
#include <algorithm>
#include <cJSON.h>
#include <cstring>
#include <esp_app_desc.h>
#include <esp_app_format.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <mbedtls/pk.h>
namespace nova {
void Firmware::begin(Storage &s, const char *override) {
  storage_ = &s;
  cleanUtf8(key_, sizeof(key_), override && override[0] ? override : releasePublicKey);
  mutex_ = xSemaphoreCreateMutex();
  bytes_ = static_cast<uint8_t *>(heap_caps_malloc(8192, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}
void Firmware::message(const char *text) {
  if (!mutex_)
    return;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  cleanUtf8(status_, sizeof(status_), text);
  xSemaphoreGive(mutex_);
}
void Firmware::status(char *out, size_t capacity) {
  if (!mutex_) {
    cleanUtf8(out, capacity, "Update unavailable");
    return;
  }
  xSemaphoreTake(mutex_, portMAX_DELAY);
  cleanUtf8(out, capacity, status_);
  xSemaphoreGive(mutex_);
}
void Firmware::latest(char *out, size_t capacity) {
  if (!mutex_) {
    cleanUtf8(out, capacity, "");
    return;
  }
  xSemaphoreTake(mutex_, portMAX_DELAY);
  cleanUtf8(out, capacity, url_[0] ? version_ : "");
  xSemaphoreGive(mutex_);
}
bool Firmware::request(Work work) {
  if (!mutex_ || !bytes_ || busy() || work == Work::None ||
      ((work == Work::Install || work == Work::AutoInstall) && state_ != State::Ready) ||
      (work == Work::Download && !url_[0]))
    return false;
  if (work == Work::Install)
    message("Installing verified candidate");
  work_ = work;
  return true;
}
bool Firmware::check() {
  url_[0] = 0;
  size_ = 0;
  preparedSize_ = 0;
  releaseCandidate_ = false;
  Http http;
  uint8_t data[2049];
  size_t at = 0;
  char manifest[192];
  snprintf(manifest, sizeof(manifest), "%s/latest/download/stable.json", endpoints::releases);
  if (!http.open(manifest, nullptr, 8000, releaseRedirect)) {
    message("릴리스 정보를 받지 못했습니다 (Wi-Fi/게시 여부 확인)");
    return false;
  }
  while (at < sizeof(data) - 1) {
    const int n = http.read(data + at, sizeof(data) - 1 - at);
    if (n < 0) {
      message("Release manifest read failed");
      return false;
    }
    if (!n)
      break;
    at += n;
  }
  if (!http.complete()) {
    message("Release manifest incomplete or oversized");
    return false;
  }
  data[at] = 0;
  auto *root = strlen(reinterpret_cast<char *>(data)) == at
                   ? cJSON_ParseWithLengthOpts(reinterpret_cast<char *>(data), at + 1, nullptr, true)
                   : nullptr;
  if (!root) {
    message("Release manifest is not valid JSON");
    return false;
  }
  auto *target = cJSON_GetObjectItemCaseSensitive(root, "target"),
       *version = cJSON_GetObjectItemCaseSensitive(root, "version"),
       *url = cJSON_GetObjectItemCaseSensitive(root, "url"),
       *size = cJSON_GetObjectItemCaseSensitive(root, "size"),
       *hash = cJSON_GetObjectItemCaseSensitive(root, "sha256");
  int order = 0;
  bool ok = cJSON_IsString(target) && !strcmp(target->valuestring, "milestone-nova-s3") &&
            cJSON_IsString(version) && strlen(version->valuestring) < sizeof(version_) &&
            compareReleaseVersions(version->valuestring, board::version, order) &&
            cJSON_IsString(url) && strlen(url->valuestring) < sizeof(url_) &&
            releaseAssetUrl(url->valuestring, version->valuestring) &&
            cJSON_IsNumber(size) && size->valuedouble >= 1024 && size->valuedouble <= 6291456 &&
            size->valuedouble == uint32_t(size->valuedouble) && cJSON_IsString(hash) &&
            strlen(hash->valuestring) == 64;
  if (ok) {
    for (const char *p = hash->valuestring; *p; ++p)
      if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f')))
        ok = false;
  }
  if (ok && order > 0) {
    strcpy(url_, url->valuestring);
    strcpy(version_, version->valuestring);
    strcpy(hash_, hash->valuestring);
    size_ = size->valuedouble;
    char text[96];
    snprintf(text, sizeof(text), "새 버전 v%s 사용 가능 (현재 v%s)", version_, board::version);
    message(text);
  } else if (ok) {
    char text[64];
    snprintf(text, sizeof(text), "최신 버전입니다 (v%s)", board::version);
    message(text);
  }
  cJSON_Delete(root);
  if (!ok)
    message("올바르지 않은 릴리스 정보");
  return ok;
}
bool Firmware::download() {
  Http http;
  if (!http.open(url_, nullptr, 8000, releaseRedirect) ||
      (http.length() >= 0 && http.length() != size_)) {
    message("Download unavailable or wrong size");
    return false;
  }
  FileJob job;
  job.op = FileOp::UploadBegin;
  strcpy(job.path, "/update/candidate.bin");
  job.id = esp_random() | 1U;
  job.total = size_;
  job.enabled = true;
  if (!storage_->execute(job)) {
    message(job.error);
    return false;
  }
  const auto fail = [this, &job](const char *reason) {
    message(reason);
    job.op = FileOp::UploadAbort;
    storage_->execute(job);
    return false;
  };
  uint8_t *bytes = bytes_;
  uint32_t offset = 0;
  const uint32_t started = millis();
  SHA256Builder hash;
  hash.begin();
  while (offset < size_) {
    if (millis() - started > 300000) {
      return fail("Download timeout; current firmware retained");
    }
    const int n = http.read(bytes, std::min<uint32_t>(8192, size_ - offset));
    if (n <= 0) {
      return fail("Download interrupted; candidate not selected");
    }
    hash.add(bytes, n);
    job.op = FileOp::UploadChunk;
    job.data = bytes;
    job.length = n;
    job.offset = offset;
    job.checksum = crc32(bytes, n);
    if (!storage_->execute(job)) {
      return fail(job.error);
    }
    offset += n;
    progress_ = uint64_t(offset) * 100 / size_;
    vTaskDelay(1);
  }
  uint8_t extra;
  const int remaining = http.read(&extra, 1);
  hash.calculate();
  if (remaining != 0 || !http.complete() || hash.toString() != hash_) {
    return fail("Download SHA256 mismatch");
  }
  job.op = FileOp::UploadCommit;
  if (!storage_->execute(job)) {
    return fail(job.error);
  }
  return prepare(true);
}
bool Firmware::prepare(bool release) {
  releaseCandidate_ = false;
  mbedtls_pk_context key;
  mbedtls_pk_init(&key);
  const bool validKey = key_[0] &&
                        mbedtls_pk_parse_public_key(&key, reinterpret_cast<const uint8_t *>(key_),
                                                    strlen(key_) + 1) == 0 &&
                        mbedtls_pk_can_do(&key, MBEDTLS_PK_RSA) &&
                        mbedtls_pk_get_bitlen(&key) >= 2048 && mbedtls_pk_get_bitlen(&key) <= 4096;
  mbedtls_pk_free(&key);
  if (!validKey) {
    message("Built-in release key invalid");
    return false;
  }
  FileJob job;
  job.op = FileOp::Info;
  strcpy(job.path, "/update/candidate.bin");
  if (!storage_->execute(job) || job.total < 1024 || job.total > 6291456) {
    message("SD candidate missing or invalid size");
    return false;
  }
  const uint32_t total = job.total;
  uint8_t *bytes = bytes_;
  job.op = FileOp::Read;
  job.data = bytes;
  job.length =
      sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t);
  if (!storage_->execute(job) || job.actual != job.length) {
    message("Candidate header read failed");
    return false;
  }
  esp_image_header_t header;
  memcpy(&header, bytes, sizeof(header));
  esp_app_desc_t desc;
  memcpy(&desc, bytes + sizeof(header) + sizeof(esp_image_segment_header_t), sizeof(desc));
  if (header.magic != ESP_IMAGE_HEADER_MAGIC || header.chip_id != ESP_CHIP_ID_ESP32S3 ||
      desc.magic_word != ESP_APP_DESC_MAGIC_WORD ||
      strncmp(desc.project_name, "MILESTONE-NOVA", sizeof(desc.project_name)) ||
      !memchr(desc.version, 0, sizeof(desc.version))) {
    message("Candidate is not a NOVA ESP32-S3 image");
    return false;
  }
  if (release && strcmp(desc.version, version_)) {
    message("Release version does not match signed image");
    return false;
  }
  SHA256Builder hash;
  SHA256Builder entire;
  hash.begin();
  entire.begin();
  for (uint32_t at = 0; at < total - 512;) {
    job.offset = at;
    job.length = std::min<uint32_t>(8192, total - 512 - at);
    if (!storage_->execute(job) || job.actual != job.length) {
      message("Candidate read failed during verification");
      return false;
    }
    hash.add(bytes, job.actual);
    entire.add(bytes, job.actual);
    at += job.actual;
    progress_ = uint64_t(at) * 100 / total;
    vTaskDelay(1);
  }
  hash.calculate();
  job.offset = total - 512;
  job.length = 512;
  if (!storage_->execute(job) || job.actual != 512) {
    message("Candidate signature footer read failed");
    return false;
  }
  entire.add(bytes, 512);
  entire.calculate();
  if (release && entire.toString() != hash_) {
    message("SD candidate no longer matches the release");
    return false;
  }
  UpdaterRSAVerifier verifier(reinterpret_cast<const uint8_t *>(key_), strlen(key_) + 1,
                              HASH_SHA256);
  if (!verifier.verify(&hash, bytes, 512)) {
    message("Candidate RSA signature rejected");
    return false;
  }
  hash.getBytes(preparedHash_);
  preparedSize_ = total;
  releaseCandidate_ = release;
  message("서명 확인 완료 / 길게 OK로 설치");
  return true;
}
bool Firmware::install() {
  // Revalidate after physical confirmation: uploads may have replaced the candidate.
  uint8_t expected[32];
  memcpy(expected, preparedHash_, 32);
  const uint32_t size = preparedSize_;
  if (!prepare(releaseCandidate_) || size != preparedSize_ || memcmp(expected, preparedHash_, 32)) {
    message("Candidate changed; confirm again");
    return false;
  }
  UpdaterRSAVerifier verifier(reinterpret_cast<const uint8_t *>(key_), strlen(key_) + 1,
                              HASH_SHA256);
  UpdateClass updater;
  if (!updater.installSignature(&verifier) || !updater.begin(size, U_FLASH)) {
    message("Inactive update partition unavailable");
    return false;
  }
  FileJob job;
  job.op = FileOp::Read;
  strcpy(job.path, "/update/candidate.bin");
  uint8_t *bytes = bytes_;
  job.data = bytes;
  const uint32_t started = millis();
  for (uint32_t at = 0; at < size;) {
    job.offset = at;
    job.length = std::min<uint32_t>(8192, size - at);
    if (millis() - started > 120000 || !storage_->execute(job) || job.actual != job.length ||
        updater.write(bytes, job.actual) != job.actual) {
      updater.abort();
      message("Install failed; current firmware retained");
      return false;
    }
    at += job.actual;
    progress_ = uint64_t(at) * 100 / size;
    vTaskDelay(1);
  }
  if (!updater.end()) {
    message("Inactive image verification failed");
    return false;
  }
  message("Verified; restarting");
  return true;
}
bool Firmware::autoInstall() {
  if (!releaseCandidate_) {
    message("Automatic install requires a verified GitHub release");
    return false;
  }
  // Persist before flash. After rollback, the same release must not start a reboot loop.
  Preferences p;
  if (!p.begin("nova-update", false)) {
    message("Cannot record automatic update attempt");
    return false;
  }
  if (p.getString("attempt", "") == hash_) {
    p.end();
    message("Release already attempted; manual retry required");
    return false;
  }
  const bool saved = p.putString("attempt", hash_) == strlen(hash_) &&
                     p.getString("attempt", "") == hash_;
  p.end();
  if (!saved) {
    message("Cannot record automatic update attempt");
    return false;
  }
  return install();
}
bool Firmware::rollback() {
  const auto *previous = esp_ota_get_next_update_partition(nullptr);
  esp_app_desc_t desc;
  esp_ota_img_states_t state;
  if (!previous || esp_ota_get_partition_description(previous, &desc) != ESP_OK ||
      strncmp(desc.project_name, "MILESTONE-NOVA", sizeof(desc.project_name)) ||
      esp_ota_get_state_partition(previous, &state) != ESP_OK || state != ESP_OTA_IMG_VALID) {
    message("No previously validated NOVA slot");
    return false;
  }
  if (esp_ota_set_boot_partition(previous) != ESP_OK) {
    message("Previous partition could not be selected");
    return false;
  }
  message("Previous validated firmware selected");
  return true;
}
void Firmware::process() {
  const Work w = work_.load();
  if (w == Work::None)
    return;
  state_ = State::Working;
  progress_ = 0;
  bool ok = false;
  switch (w) {
  case Work::Check:
    ok = check();
    break;
  case Work::Download:
    ok = download();
    break;
  case Work::Prepare:
    ok = prepare();
    break;
  case Work::Install:
    ok = install();
    break;
  case Work::AutoInstall:
    ok = autoInstall();
    break;
  case Work::Rollback:
    ok = rollback();
    break;
  default:
    break;
  }
  state_ = !ok ? State::Failed
           : w == Work::Check ? (url_[0] ? State::Available : State::Current)
           : w == Work::Install || w == Work::AutoInstall || w == Work::Rollback ? State::Success
                                                                                : State::Ready;
  work_ = Work::None;
}
} // namespace nova

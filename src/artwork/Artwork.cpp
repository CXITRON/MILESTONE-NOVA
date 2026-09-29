#include "Artwork.h"
#include "../core/Text.h"
#include "../network/Endpoints.h"
#include "../network/Http.h"
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_timer.h>
namespace nova {
namespace {
bool formPart(char *out, size_t capacity, size_t &at, const char *s) {
  constexpr char hex[] = "0123456789ABCDEF";
  for (const auto *p = reinterpret_cast<const uint8_t *>(s); *p; ++p) {
    const bool plain =
        (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9');
    if (at + (plain ? 1 : 3) >= capacity)
      return false;
    if (plain)
      out[at++] = *p;
    else {
      out[at++] = '%';
      out[at++] = hex[*p >> 4];
      out[at++] = hex[*p & 15];
    }
  }
  out[at] = 0;
  return true;
}
} // namespace
bool Artwork::begin(Storage &s) {
  storage_ = &s;
  mutex_ = xSemaphoreCreateMutex();
  packet_ = static_cast<uint8_t *>(heap_caps_malloc(22705, MALLOC_CAP_SPIRAM));
  image_ = static_cast<uint8_t *>(heap_caps_malloc(51216, MALLOC_CAP_SPIRAM));
  result_ = static_cast<uint16_t *>(heap_caps_malloc(51200, MALLOC_CAP_SPIRAM));
  return mutex_ && packet_ && image_ && result_;
}
bool Artwork::request(const Track &t, uint32_t gen, const Settings &s) {
  if (!mutex_ || !packet_ || !image_ || !result_)
    return false;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  track_ = t;
  requested_ = gen;
  settings_ = s;
  pending_ = true;
  xSemaphoreGive(mutex_);
  return true;
}
bool Artwork::receive(uint16_t *pixels, uint32_t gen, bool &ok) {
  if (!mutex_ || !pixels || xSemaphoreTake(mutex_, 0) != pdTRUE)
    return false;
  const bool available = ready_ && delivered_ == gen;
  if (available) {
    ok = ok_;
    if (ok)
      memcpy(pixels, result_, 51200);
  }
  ready_ = false;
  xSemaphoreGive(mutex_);
  return available;
}
bool Artwork::fetch(const Track &query, uint16_t *pixels) {
  if (!packet_ || !pixels || !query.title[0] || !query.artist[0])
    return false;
  // Existing Worker accepts at most 1400 encoded bytes, including field separators.
  char form[1401] = "title=";
  size_t at = 6;
  if (!formPart(form, sizeof(form) - 15, at, query.title))
    return false;
  strcpy(form + at, "&artist=");
  at += 8;
  if (!formPart(form, sizeof(form) - 7, at, query.artist))
    return false;
  strcpy(form + at, "&album=");
  at += 7;
  if (!formPart(form, sizeof(form), at, query.album))
    return false;
  const int64_t started = esp_timer_get_time();
  Http http;
  if (!http.open(endpoints::artwork, form) || (http.length() >= 0 && http.length() != 22704))
    return false;
  size_t received = 0;
  while (received < 22705) {
    // A slow trickle must not keep the service worker (and OFF) occupied indefinitely.
    if (esp_timer_get_time() - started >= 15000000)
      return false;
    const int n = http.read(packet_ + received, 22705 - received);
    if (n < 0)
      return false;
    if (!n)
      break;
    received += n;
  }
  return http.complete() && artworkPacket(packet_, received, pixels, 160);
}
void Artwork::remember(const Track &track) {
  FileJob job;
  job.op = FileOp::ArtRemember;
  job.data = reinterpret_cast<uint8_t *>(const_cast<Track *>(&track));
  job.length = sizeof(track);
  storage_->execute(job);
}
bool Artwork::save(const char *key, const uint16_t *pixels, bool custom, const Settings &s) {
  if (!image_ || !validTrackKey(key))
    return false;
  FileJob quota;
  quota.op = FileOp::ArtCleanup;
  quota.total = s.artworkCacheMb;
  // Evict only excess cache, never to satisfy low-free-space pressure.
  for (unsigned i = 0; i < 256; ++i) {
    quota.value = 0;
    if (!storage_->execute(quota))
      return false;
    if (!quota.value)
      break;
  }
  if (quota.capacity < uint64_t(s.artworkFreeMb) * 1048576 + 51216)
    return false;
  memset(image_, 0, 16);
  memcpy(image_, "NVI1", 4);
  write16(image_ + 4, 160);
  write16(image_ + 6, 160);
  memmove(image_ + 16, pixels, 51200);
  write32(image_ + 8, 51200);
  write32(image_ + 12, crc32(image_ + 16, 51200));
  FileJob job;
  job.op = FileOp::ArtSave;
  job.enabled = custom;
  snprintf(job.path, sizeof(job.path), "/artwork/%s.nvi", key);
  job.data = image_;
  job.length = 51216;
  if (!storage_->execute(job))
    return false;
  char marker[128];
  snprintf(marker, sizeof(marker), "/artwork/%s.missing", key);
  FileJob missing;
  missing.op = FileOp::Remove;
  strcpy(missing.path, marker);
  storage_->execute(missing);
  if (custom) {
    snprintf(missing.path, sizeof(missing.path), "/artwork/%s.block", key);
    storage_->execute(missing);
    job.op = FileOp::ArtFlags;
    job.value = 0;
    job.enabled = true;
    return storage_->execute(job);
  }
  return true;
}
void Artwork::process() {
  if (!mutex_ || !result_)
    return;
  Track track;
  Settings settings;
  uint32_t gen;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (!pending_) {
    xSemaphoreGive(mutex_);
    return;
  }
  track = track_;
  settings = settings_;
  gen = requested_;
  pending_ = false;
  ready_ = false;
  xSemaphoreGive(mutex_);
  // image_ is worker-owned scratch; result_ is published under the mutex.
  auto *pixels = reinterpret_cast<uint16_t *>(image_ + 16);
  remember(track);
  FileJob policy;
  policy.op = FileOp::ArtAllowed;
  snprintf(policy.path, sizeof(policy.path), "/artwork/%s.nvi", track.key);
  // A queued lookup must honor protection added while the setup portal was open.
  const bool allowed = !storage_->mounted() || storage_->execute(policy);
  const bool ok = allowed && fetch(track, pixels);
  if (ok) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    memcpy(result_, pixels, 51200);
    xSemaphoreGive(mutex_);
    save(track.key, pixels, false, settings);
  }
  xSemaphoreTake(mutex_, portMAX_DELAY);
  delivered_ = gen;
  ok_ = ok;
  ready_ = true;
  xSemaphoreGive(mutex_);
}
} // namespace nova

#include "OnlineLyrics.h"
#include "../core/Text.h"
#include "../network/Endpoints.h"
#include "../network/Http.h"
#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <new>
namespace nova {
bool OnlineLyrics::begin(Storage &s) {
  storage_ = &s;
  mutex_ = xSemaphoreCreateMutex();
  text_ = static_cast<char *>(heap_caps_malloc(maxLyricsBytes + 1, MALLOC_CAP_SPIRAM));
  void *memory = heap_caps_malloc(sizeof(Lyrics), MALLOC_CAP_SPIRAM);
  if (memory)
    result_ = new (memory) Lyrics{};
  return mutex_ && text_ && result_;
}
bool OnlineLyrics::request(const Track &t, uint32_t gen) {
  if (!mutex_ || !text_ || !result_)
    return false;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  track_ = t;
  requested_ = gen;
  pending_ = true;
  xSemaphoreGive(mutex_);
  return true;
}
bool OnlineLyrics::receive(Lyrics &out, uint32_t gen, bool &found) {
  if (!mutex_ || xSemaphoreTake(mutex_, 0) != pdTRUE)
    return false;
  const bool available = ready_ && delivered_ == gen;
  if (available) {
    found = found_;
    if (found)
      out = *result_;
  }
  ready_ = false;
  xSemaphoreGive(mutex_);
  return available;
}
bool OnlineLyrics::fetch(const Track &query, size_t &bytes) {
  bytes = 0;
  if (!text_ || !result_ || !query.title[0] || !query.artist[0])
    return false;
  // The gateway accepts at most 1400 encoded bytes, including field separators.
  char form[1401] = "title=";
  size_t at = 6;
  char duration[24];
  const auto seconds = static_cast<unsigned long>((uint64_t(query.durationMs) + 500) / 1000);
  if (seconds > 3600)
    return false;
  snprintf(duration, sizeof(duration), "&duration=%lu", seconds ? seconds : 1UL);
  const size_t tail = query.durationMs ? strlen(duration) : 0;
  if (!appendFormValue(form, sizeof(form) - 15 - tail, at, query.title))
    return false;
  strcpy(form + at, "&artist=");
  at += 8;
  if (!appendFormValue(form, sizeof(form) - 7 - tail, at, query.artist))
    return false;
  strcpy(form + at, "&album=");
  at += 7;
  if (!appendFormValue(form, sizeof(form) - tail, at, query.album))
    return false;
  if (tail) {
    strcpy(form + at, duration);
    at += tail;
  }
  const int64_t started = esp_timer_get_time();
  Http http;
  // Worker lookup has a 6 s deadline; allow the response headers to arrive before timing out.
  if (!http.open(endpoints::lyrics, form, 8000) || http.length() > int64_t(maxLyricsBytes))
    return false;
  while (bytes <= maxLyricsBytes) {
    // A slow trickle must not keep the service worker (and OFF) occupied indefinitely.
    if (esp_timer_get_time() - started >= 15000000)
      return false;
    const int n = http.read(reinterpret_cast<uint8_t *>(text_) + bytes, maxLyricsBytes + 1 - bytes);
    if (n < 0)
      return false;
    if (!n)
      break;
    bytes += n;
  }
  if (!bytes || bytes > maxLyricsBytes || !http.complete())
    return false;
  text_[bytes] = 0;
  if (strlen(text_) != bytes || !validUtf8({text_, bytes}))
    return false;
  LrcParser{}.parse({text_, bytes}, *result_);
  return result_->synced && result_->count && !result_->rejected && !result_->truncated;
}
void OnlineLyrics::process() {
  if (!mutex_ || !text_)
    return;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (!pending_) {
    xSemaphoreGive(mutex_);
    return;
  }
  const Track track = track_;
  const uint32_t gen = requested_;
  pending_ = ready_ = false;
  xSemaphoreGive(mutex_);
  FileJob job;
  bool found = false;
  size_t bytes = 0;
  if (LyricsCache::path(job.path, sizeof(job.path), track.key)) {
    // Skip known local files, then recheck at the single SD writer before committing.
    job.op = FileOp::Info;
    const bool local = storage_->mounted() && storage_->execute(job);
    found = !local && fetch(track, bytes);
    if (found && storage_->mounted()) {
      job.op = FileOp::LyricsSave;
      job.data = reinterpret_cast<uint8_t *>(text_);
      job.length = bytes;
      // I/O failure still permits RAM display; a newly arrived local file takes precedence.
      if (!storage_->execute(job) && job.value == 1)
        found = false;
    }
  }
  xSemaphoreTake(mutex_, portMAX_DELAY);
  found_ = found;
  delivered_ = gen;
  ready_ = true;
  xSemaphoreGive(mutex_);
}
} // namespace nova

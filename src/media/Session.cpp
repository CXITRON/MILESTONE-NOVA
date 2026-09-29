#include "Session.h"
#include "../core/Text.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace nova {
bool MediaSession::replace(const Track &t, uint32_t now) {
  Track next = t;
  next.title[240] = next.artist[240] = next.album[240] = next.key[16] = 0;
  if (!validTrackKey(next.key))
    trackKey(next.key, next.artist, next.title, next.album, next.durationMs);
  const bool change = strcmp(next.key, track_.key) != 0;
  if (change) {
    ++generation_;
    anchorMs_ = now;
    anchorPosition_ = 0;
    playing_ = false;
    known_ = false;
  }
  track_ = next;
  return change;
}
void MediaSession::synchronize(uint32_t p, bool playing, uint32_t now, float rate) {
  anchorPosition_ = track_.durationMs ? std::min(p, track_.durationMs) : p;
  anchorMs_ = now;
  playing_ = playing;
  known_ = true;
  rate_ = std::isfinite(rate) ? std::clamp(rate, 0.0f, 4.0f) : 1.0f;
}
uint32_t MediaSession::position(uint32_t now) const {
  uint64_t p = anchorPosition_;
  if (known_ && playing_)
    p += static_cast<uint64_t>((now - anchorMs_) * static_cast<double>(rate_));
  if (track_.durationMs)
    p = std::min<uint64_t>(p, track_.durationMs);
  return static_cast<uint32_t>(std::min<uint64_t>(p, UINT32_MAX));
}
void MediaSession::disconnect(uint32_t now) {
  player_ = PlayerInfo{};
  anchorPosition_ = position(now);
  anchorMs_ = now;
  playing_ = false;
}
} // namespace nova

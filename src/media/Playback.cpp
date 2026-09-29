#include "Playback.h"
#include <algorithm>
namespace nova {
void Playback::load(uint32_t duration, uint32_t now) {
  duration_ = duration;
  position_ = 0;
  anchor_ = now;
  session_ = 0;
  playing_ = sequenced_ = stale_ = false;
}
uint32_t Playback::position(uint32_t now) const {
  return std::min<uint64_t>(duration_,
                            uint64_t(position_) + (playing_ ? uint32_t(now - anchor_) : 0));
}
void Playback::seek(uint32_t value, bool playing, uint32_t now) {
  position_ = std::min(value, duration_);
  anchor_ = now;
  playing_ = playing && duration_ && position_ < duration_;
}
void Playback::toggle(uint32_t now) {
  if (session_)
    return;
  auto p = position(now);
  if (!playing_ && p >= duration_)
    p = 0;
  seek(p, !playing_, now);
}
void Playback::startSync(uint32_t session, uint32_t duration, uint32_t now) {
  load(duration, now);
  session_ = session;
  last_ = now;
}
bool Playback::sync(uint32_t session, uint32_t sequence, uint32_t p, bool playing, uint32_t now) {
  if (!session || session != session_ || (sequenced_ && int32_t(sequence - sequence_) <= 0))
    return false;
  sequence_ = sequence;
  sequenced_ = true;
  last_ = now;
  stale_ = false;
  seek(p, playing, now);
  return true;
}
void Playback::stopSync(uint32_t now) {
  seek(position(now), false, now);
  session_ = 0;
  stale_ = false;
}
void Playback::tick(uint32_t now, bool loop) {
  if (session_ && now - last_ > 2500 && playing_) {
    // Freeze at the deadline, never continue drifting after losing the controller.
    seek(position(last_ + 2500), false, now);
    stale_ = true;
  }
  if (playing_ && position(now) >= duration_) {
    if (loop && !session_ && duration_) {
      const uint64_t elapsed = uint64_t(position_) + uint32_t(now - anchor_);
      seek(elapsed % duration_, true, now);
    } else
      seek(duration_, false, now);
  }
}
} // namespace nova

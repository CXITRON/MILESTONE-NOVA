#pragma once
#include <cstdint>
namespace nova {
// One clock contract for local playback and browser-authoritative SD playback.
class Playback {
public:
  void load(uint32_t duration, uint32_t now);
  void toggle(uint32_t now);
  void seek(uint32_t position, bool playing, uint32_t now);
  bool sync(uint32_t session, uint32_t sequence, uint32_t position, bool playing, uint32_t now);
  void startSync(uint32_t session, uint32_t duration, uint32_t now);
  void stopSync(uint32_t now);
  void tick(uint32_t now, bool loop);
  uint32_t position(uint32_t now) const;
  uint32_t duration() const { return duration_; }
  bool playing() const { return playing_; }
  bool synchronized() const { return session_ != 0; }
  bool stale() const { return stale_; }
  uint32_t session() const { return session_; }

private:
  uint32_t duration_ = 0, position_ = 0, anchor_ = 0, session_ = 0, sequence_ = 0, last_ = 0;
  bool playing_ = false, sequenced_ = false, stale_ = false;
};
} // namespace nova

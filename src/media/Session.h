#pragma once
#include <cstdint>
namespace nova {
struct Track {
  char key[17]{}, title[241]{}, artist[241]{}, album[241]{};
  uint32_t durationMs = 0;
};
struct PlayerInfo {
  char name[65]{};
  float volume = 0;
  uint32_t queueIndex = 0, queueCount = 0;
  uint8_t shuffle = 0, repeat = 0;
  bool volumeKnown = false;
};
class MediaSession {
public:
  bool replace(const Track &track, uint32_t now);
  void synchronize(uint32_t position, bool playing, uint32_t now, float rate = 1.0f);
  void disconnect(uint32_t now);
  uint32_t position(uint32_t now) const;
  const Track &track() const { return track_; }
  uint32_t generation() const { return generation_; }
  bool playing() const { return playing_; }
  const PlayerInfo &player() const { return player_; }
  void player(const PlayerInfo &info) { player_ = info; }
  bool positionKnown() const { return known_; }

private:
  Track track_{};
  PlayerInfo player_{};
  uint32_t anchorMs_ = 0, anchorPosition_ = 0, generation_ = 0;
  float rate_ = 1.0f;
  bool playing_ = false, known_ = false;
};
} // namespace nova

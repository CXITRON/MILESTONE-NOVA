#pragma once
#include "Session.h"
#include <cstddef>
namespace nova {
inline constexpr char helperService[] = "33b2a140-9d4a-4e1b-b952-a1d398c30001";
inline constexpr char helperInput[] = "33b2a140-9d4a-4e1b-b952-a1d398c30002";
inline constexpr char helperControl[] = "33b2a140-9d4a-4e1b-b952-a1d398c30003";
class HelperDecoder {
public:
  bool accept(const uint8_t *bytes, size_t length, uint32_t now);
  void reset();
  const Track &track() const { return track_; }
  uint32_t position() const { return position_; }
  bool playing() const { return playing_; }

private:
  Track track_{};
  uint32_t transaction_ = 0, started_ = 0, position_ = 0;
  uint8_t fields_ = 0;
  bool playing_ = false;
};
} // namespace nova

#pragma once
#include "Session.h"
#include <cstddef>
namespace nova {
// AMS publishes attributes independently. Commit a bounded quiet burst as one track.
class AmsDecoder {
public:
  void reset();
  void accept(const uint8_t *bytes, size_t size, uint32_t receivedAt, MediaSession &session);
  void tick(uint32_t now, MediaSession &session);

private:
  Track pending_{};
  bool dirty_ = false, positionAvailable_ = false, playing_ = false;
  uint32_t changedAt_ = 0, positionAt_ = 0, position_ = 0;
  float rate_ = 1;
};
} // namespace nova

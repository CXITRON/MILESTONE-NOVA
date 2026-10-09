#pragma once
#include "Effects.h"
#include "LedSignal.h"
namespace nova {
class Rgb {
public:
  void begin();
  void tick(uint32_t now, uint8_t limit, const LightState &state);
  void off();
  // True when the whole strip fits in RMT channel memory, so a frame is never refilled mid-send.
  bool whole() const { return whole_; }

private:
  void show(const LightFrame &frame);
  LightEffects effects_;
  LedSignal signal_{};
  uint32_t last_ = 0;
  bool ready_ = false, whole_ = false;
};
} // namespace nova

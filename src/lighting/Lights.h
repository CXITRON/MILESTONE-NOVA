#pragma once
#include "Effects.h"
#include <Adafruit_NeoPixel.h>
namespace nova {
class Rgb {
public:
  void begin();
  void tick(uint32_t now, uint8_t limit, const LightState &state);
  void off();

private:
  Adafruit_NeoPixel pixels_{board::rgbCount, board::rgb, NEO_GRB + NEO_KHZ800};
  LightEffects effects_;
  uint32_t last_ = 0;
  bool dark_ = false;
  uint8_t fastFrames_ = 0;
};
} // namespace nova

#pragma once
#include "../board/Board.h"
#include <Adafruit_NeoPixel.h>
namespace nova {
class Rgb {
public:
  void begin();
  void tick(uint32_t now, uint8_t limit, bool playing, bool timer, bool finished, bool warning,
            bool sleeping);
  void off();

private:
  Adafruit_NeoPixel pixels_{board::rgbCount, board::rgb, NEO_GRB + NEO_KHZ800};
  uint32_t last_ = 0;
};
} // namespace nova

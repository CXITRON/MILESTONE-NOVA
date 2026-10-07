#pragma once
#include <array>
#include <cstdint>
inline constexpr int NEO_GRB = 0, NEO_KHZ800 = 0;
namespace mockLights {
inline unsigned shows = 0;
inline bool ready = true;
inline std::array<std::array<uint8_t, 3>, 5> pixels{};
}
inline void rgbLedWrite(int, int, int, int) {}
class Adafruit_NeoPixel {
public:
  Adafruit_NeoPixel(unsigned, int, int) {}
  void begin() {}
  void setBrightness(uint8_t) {}
  void clear() { mockLights::pixels = {}; }
  void show() { ++mockLights::shows; }
  bool canShow() { return mockLights::ready; }
  void setPixelColor(unsigned i, uint8_t r, uint8_t g, uint8_t b) {
    mockLights::pixels.at(i) = {r, g, b};
  }
};

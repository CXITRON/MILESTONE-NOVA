#include "Lights.h"
#include <cmath>
namespace nova {
void Rgb::begin() {
  pixels_.begin();
  pixels_.setBrightness(255);
  off();
  rgbLedWrite(board::onboardRgb, 0, 0, 0);
}
void Rgb::off() {
  pixels_.clear();
  pixels_.show();
}
void Rgb::tick(uint32_t now, uint8_t limit, bool playing, bool timer, bool finished, bool warning,
               bool sleeping) {
  if (now - last_ < 40)
    return;
  last_ = now;
  // `i` is the visual position from the left; map it to the chain index of that LED.
  for (unsigned i = 0; i < board::rgbCount; ++i) {
    const unsigned pixel = board::rgbFromRight ? board::rgbCount - 1 - i : i;
    float amplitude = 0.2f;
    if (now < 1800 || playing || timer || finished)
      amplitude = (1 + std::sin(now / 650.0f - i * 0.6f)) * 0.5f;
    if (sleeping)
      amplitude = 0.05f;
    const auto v = static_cast<uint8_t>(limit * amplitude);
    pixels_.setPixelColor(pixel,
                          warning    ? v
                          : finished ? v / 2
                                     : 0,
                          warning ? 0 : v,
                          warning ? 0
                          : timer ? v / 5
                                  : v / 2);
  }
  if (pixels_.canShow())
    pixels_.show();
}
} // namespace nova

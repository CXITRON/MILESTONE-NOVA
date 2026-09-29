#include "Lights.h"
#include <cmath>
namespace nova {
void StatusLeds::begin() {
  ledcAttach(board::red, 5000, 8);
  ledcAttach(board::green, 5000, 8);
  off();
}
void StatusLeds::off() {
  ledcWrite(board::red, 0);
  ledcWrite(board::green, 0);
}
void StatusLeds::tick(uint32_t now, uint8_t brightness, bool warning, bool critical,
                      bool updating) {
  const float breath = (1 - std::cos((now % 4000) * 6.2831853f / 4000)) * 0.5f;
  ledcWrite(board::green,
            critical ? 0 : static_cast<uint8_t>(brightness * (0.08f + 0.92f * breath)));
  ledcWrite(board::red, critical   ? 96
                        : warning  ? ((now % 2000) < 600 ? 48 : 0)
                        : updating ? 12
                                   : 0);
}
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
  for (unsigned i = 0; i < board::rgbCount; ++i) {
    float amplitude = 0.2f;
    if (now < 1800 || playing || timer || finished)
      amplitude = (1 + std::sin(now / 650.0f - i * 0.6f)) * 0.5f;
    if (sleeping)
      amplitude = 0.05f;
    const auto v = static_cast<uint8_t>(limit * amplitude);
    pixels_.setPixelColor(i,
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

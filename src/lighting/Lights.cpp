#include "Lights.h"
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
void Rgb::tick(uint32_t now, uint8_t limit, const LightState &state) {
  if (now - last_ < 40 || !pixels_.canShow())
    return;
  last_ = now;
  const auto frame = effects_.render(now, limit, state);
  for (unsigned i = 0; i < frame.size(); ++i) {
    const unsigned pixel = board::rgbFromRight ? board::rgbCount - 1 - i : i;
    pixels_.setPixelColor(pixel, frame[i].r, frame[i].g, frame[i].b);
  }
  pixels_.show();
}
} // namespace nova

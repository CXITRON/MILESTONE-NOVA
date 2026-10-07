#include "Lights.h"
#include <algorithm>
namespace nova {
void Rgb::begin() {
  pixels_.begin();
  pixels_.setBrightness(255);
  off();
  rgbLedWrite(board::onboardRgb, 0, 0, 0);
}
void Rgb::off() {
  effects_.resetOutput();
  fastFrames_ = 0;
  pixels_.clear();
  pixels_.show();
  dark_ = true;
}
void Rgb::tick(uint32_t now, uint8_t limit, const LightState &state) {
  // Clear once, immediately, without wasting bus time or advancing dithering while off.
  if (!limit || state.sleeping) {
    if (!dark_) off();
    return;
  }
  // 125 Hz while lit; no catch-up bursts when the main loop is busy with video.
  if ((!dark_ && now - last_ < 8) || !pixels_.canShow())
    return;
  // A video frame can stall the main loop. Do not diffuse error at that low cadence:
  // require three consecutive <=16 ms frames before resuming temporal dithering.
  const uint32_t elapsed = dark_ ? 8 : now - last_;
  fastFrames_ = elapsed <= 16 ? std::min<unsigned>(3, fastFrames_ + 1) : 0;
  last_ = now;
  dark_ = false;
  const auto frame = effects_.render(now, limit, state, fastFrames_ >= 3);
  for (unsigned i = 0; i < frame.size(); ++i) {
    const unsigned pixel = board::rgbFromRight ? board::rgbCount - 1 - i : i;
    pixels_.setPixelColor(pixel, frame[i].r, frame[i].g, frame[i].b);
  }
  pixels_.show();
}
} // namespace nova

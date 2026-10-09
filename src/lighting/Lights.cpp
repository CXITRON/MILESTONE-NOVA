#include "Lights.h"
#include <Arduino.h>
namespace nova {
static_assert(sizeof(rmt_data_t) == sizeof(uint32_t), "LedSignal words must match rmt_data_t");
void Rgb::begin() {
  // 120 symbols for five LEDs. One RMT block holds 48, so the old driver refilled channel memory
  // from an interrupt while sending; a late refill (the LCD DMA interrupts share this core) sends
  // stale symbols and the LEDs flash wrong colors. Three blocks (144) hold the whole frame.
  static_assert(ledSymbols < 3 * SOC_RMT_MEM_WORDS_PER_CHANNEL, "strip must fit three RMT blocks");
  whole_ = rmtInit(board::rgb, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_3, 10000000);
  ready_ = whole_ || rmtInit(board::rgb, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 10000000);
  off();
  rgbLedWrite(board::onboardRgb, 0, 0, 0);
}
void Rgb::show(const LightFrame &frame) {
  if (!ready_)
    return;
  encodeLeds(frame, signal_);
  rmtWrite(board::rgb, reinterpret_cast<rmt_data_t *>(signal_.data()), signal_.size(),
           RMT_WAIT_FOR_EVER);
}
void Rgb::off() { show(LightFrame{}); }
void Rgb::tick(uint32_t now, uint8_t limit, const LightState &state) {
  if (now - last_ < 40)
    return;
  last_ = now;
  show(effects_.render(now, limit, state));
}
} // namespace nova

#pragma once
#include "Effects.h"
#include <array>
#include <cstddef>
#include <cstdint>
namespace nova {
// WS2812 waveform for the whole strip, built before transmission. At 10 MHz one tick is 0.1 us:
// a 1 bit is 0.8 us high + 0.4 us low, a 0 bit 0.4 us high + 0.8 us low (same timing as the
// Adafruit NeoPixel driver this replaces). Each word uses the Arduino `rmt_data_t` layout:
// duration0 bits 0-14, level0 bit 15, duration1 bits 16-30, level1 bit 31.
inline constexpr size_t ledSymbols = board::rgbCount * 24;
using LedSignal = std::array<uint32_t, ledSymbols>;
inline constexpr uint32_t ledOne = 8u | 1u << 15 | 4u << 16;
inline constexpr uint32_t ledZero = 4u | 1u << 15 | 8u << 16;
inline void encodeLeds(const LightFrame &frame, LedSignal &out) {
  size_t symbol = 0;
  for (unsigned pixel = 0; pixel < frame.size(); ++pixel) {
    // `frame` is in effect order; the strip is wired from the right.
    const auto &c = frame[board::rgbFromRight ? frame.size() - 1 - pixel : pixel];
    for (const uint8_t byte : {c.g, c.r, c.b})  // WS2812 expects GRB, most significant bit first
      for (int bit = 7; bit >= 0; --bit)
        out[symbol++] = byte >> bit & 1 ? ledOne : ledZero;
  }
}
} // namespace nova

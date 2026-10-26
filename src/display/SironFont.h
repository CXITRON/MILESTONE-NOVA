#pragma once
#include <cstdint>
namespace nova::siron {
// Siron letters as 16 px bitmaps. The conlang font uses U+F0000..; Unifont paths cannot draw
// above U+FFFF, so the device text uses private-use U+E000..E029 in charmap.tsv order.
inline constexpr uint32_t firstCodepoint = 0xE000;
inline constexpr unsigned glyphCount = 42;
struct Glyph {
  uint8_t width;      // advance in pixels, including the side bearings
  uint16_t rows[16];  // bit 15 is the leftmost pixel; row 13 is the baseline
};
const Glyph *glyph(uint32_t codepoint);
} // namespace nova::siron

#pragma once
#include "SironFont.h"
namespace nova::jamo {
// Hangul compatibility jamo (U+3131..U+3163). The Korean Unifont face in U8g2 has the 2,350
// common syllables but no lone jamo, which the Morse memo shows while a syllable is open.
// Same 16 px cell and layout as the Siron glyphs.
const siron::Glyph *glyph(uint32_t codepoint);
} // namespace nova::jamo

#include "Canvas.h"
#include "../core/Text.h"
#include <algorithm>
#include <cstring>
#ifdef ARDUINO
#include <esp_heap_caps.h>
#else
#include <cstdlib>
#endif
namespace nova {
Canvas::~Canvas() { free(pixels_); }
bool Canvas::begin() {
#ifdef ARDUINO
  pixels_ = static_cast<uint16_t *>(
      heap_caps_calloc(board::width * board::height, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
  pixels_ = static_cast<uint16_t *>(calloc(board::width * board::height, 2));
#endif
  static const u8g2_cb_t cb{nullptr, nullptr, glyphLine};
  font_.cb = &cb;
  font_.owner = this;
  font_.width = font_.height = font_.user_x1 = font_.user_y1 = 64;
  font_.clip_x1 = font_.clip_y1 = 64;
  font_.is_page_clip_window_intersection = 1;
  u8g2_SetFont(&font_, u8g2_font_unifont_t_korean2);
  u8g2_SetFontMode(&font_, 1);
  u8g2_SetFontPosBaseline(&font_);
  u8g2_SetDrawColor(&font_, 1);
  return pixels_ != nullptr;
}
void Canvas::clear(uint16_t color) {
  if (pixels_)
    std::fill(pixels_, pixels_ + board::width * board::height, color);
}
void Canvas::pixel(int x, int y, uint16_t color) {
  if (pixels_ && x >= x0_ && x < x1_ && y >= y0_ && y < y1_)
    pixels_[y * board::width + x] = color;
}
void Canvas::rect(int x, int y, int w, int h, uint16_t color) {
  if (!pixels_)
    return;
  const int endX = std::min(x + w, x1_), endY = std::min(y + h, y1_);
  x = std::max(x, x0_);
  y = std::max(y, y0_);
  if (x >= endX)
    return;
  for (; y < endY; ++y)
    std::fill(pixels_ + y * board::width + x, pixels_ + y * board::width + endX, color);
}
void Canvas::image(int x, int y, int w, int h, const uint16_t *data, bool monochrome) {
  if (!data)
    return;
  if (!monochrome) {
    // Video frames land here every frame: clip once and copy whole rows instead of per-pixel writes.
    if (!pixels_)
      return;
    const int startX = std::max(x, x0_), endX = std::min(x + w, x1_);
    const int startY = std::max(y, y0_), endY = std::min(y + h, y1_);
    for (int row = startY; row < endY && startX < endX; ++row)
      memcpy(pixels_ + row * board::width + startX, data + (row - y) * w + (startX - x),
             size_t(endX - startX) * sizeof(uint16_t));
    return;
  }
  for (int row = 0; row < h; ++row)
    for (int col = 0; col < w; ++col) {
      uint16_t p = data[row * w + col];
      if (monochrome) {
        const unsigned light = (((p >> 11) * 255 / 31) * 77 + (((p >> 5) & 63) * 255 / 63) * 150 +
                                (p & 31) * 255 / 31 * 29) >>
                               8;
        p = ((light >> 3) << 11) | ((light >> 2) << 5) | (light >> 3);
      }
      pixel(x + col, y + row, p);
    }
}
void Canvas::glyphLine(u8g2_t *f, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t n, uint8_t dir) {
  auto &c = *static_cast<FontContext *>(f)->owner;
  if (f->draw_color)
    c.rect(c.ox_ + int(x) * c.scale_, c.oy_ + int(y) * c.scale_, (dir ? 1 : n) * c.scale_,
           (dir ? n : 1) * c.scale_, c.ink_);
}
namespace {
// Punctuation that neither Unifont face has (common in track titles) is drawn as the closest
// ASCII shape instead of '?'. Stored text, metadata and track keys are never changed.
uint32_t asciiLookalike(uint32_t cp) {
  switch (cp) {
  case 0x00b7: // middle dot
  case 0x2022: // bullet
  case 0x2027:
  case 0x2219:
  case 0x22c5:
  case 0x2010: // hyphens, en/em dashes, horizontal bar, minus
  case 0x2011:
  case 0x2012:
  case 0x2013:
  case 0x2014:
  case 0x2015:
  case 0x2212:
    return '-';
  case 0x2018: // single quotes and prime
  case 0x2019:
  case 0x201a:
  case 0x201b:
  case 0x2032:
    return '\'';
  case 0x201c: // double quotes and double prime
  case 0x201d:
  case 0x201e:
  case 0x2033:
    return '"';
  case 0x2026: // ellipsis
    return '.';
  case 0x00b0: // degree
    return 'o';
  case 0x00d7: // multiplication
    return 'x';
  case 0x2190:
    return '<';
  case 0x2192:
    return '>';
  case 0x2191:
    return '^';
  case 0x2193:
    return 'v';
  case 0x00a0: // spaces
  case 0x2002:
  case 0x2003:
  case 0x2009:
  case 0x202f:
    return ' ';
  default:
    return '?';
  }
}
} // namespace
int Canvas::selectGlyph(uint32_t &cp) {
  // Unifont Japanese omits halfwidth kana. Render their fullwidth equivalents
  // without changing stored metadata, track keys or lyric text.
  static constexpr uint16_t halfwidthKana[]{
      0x3002, 0x300c, 0x300d, 0x3001, 0x30fb, 0x30f2, 0x30a1, 0x30a3, 0x30a5,
      0x30a7, 0x30a9, 0x30e3, 0x30e5, 0x30e7, 0x30c3, 0x30fc, 0x30a2, 0x30a4,
      0x30a6, 0x30a8, 0x30aa, 0x30ab, 0x30ad, 0x30af, 0x30b1, 0x30b3, 0x30b5,
      0x30b7, 0x30b9, 0x30bb, 0x30bd, 0x30bf, 0x30c1, 0x30c4, 0x30c6, 0x30c8,
      0x30ca, 0x30cb, 0x30cc, 0x30cd, 0x30ce, 0x30cf, 0x30d2, 0x30d5, 0x30d8,
      0x30db, 0x30de, 0x30df, 0x30e0, 0x30e1, 0x30e2, 0x30e4, 0x30e6, 0x30e8,
      0x30e9, 0x30ea, 0x30eb, 0x30ec, 0x30ed, 0x30ef, 0x30f3, 0x309b, 0x309c,
  };
  if (cp >= 0xff61 && cp <= 0xff9f)
    cp = halfwidthKana[cp - 0xff61];
  if (cp > 0xffff)
    cp = '?';
  // Prefer Japanese forms for kana/kanji; keep the existing Korean/Latin glyphs.
  const bool japanese = (cp >= 0x2e80 && cp <= 0x9fff) ||
                        (cp >= 0xf900 && cp <= 0xfaff) || (cp >= 0xff00 && cp <= 0xffef);
  const auto *primary = japanese ? u8g2_font_unifont_t_japanese3 : u8g2_font_unifont_t_korean2;
  const auto *fallback = japanese ? u8g2_font_unifont_t_korean2 : u8g2_font_unifont_t_japanese3;
  u8g2_SetFont(&font_, primary);
  int advance = u8g2_GetGlyphWidth(&font_, cp);
  if (advance <= 0) {
    u8g2_SetFont(&font_, fallback);
    advance = u8g2_GetGlyphWidth(&font_, cp);
  }
  if (advance <= 0) {
    cp = asciiLookalike(cp);
    u8g2_SetFont(&font_, u8g2_font_unifont_t_korean2);
    advance = u8g2_GetGlyphWidth(&font_, cp);
  }
  return advance;
}
int Canvas::textWidth(std::string_view text, int scale) {
  size_t p = 0;
  int w = 0;
  while (p < text.size()) {
    auto cp = nextCodepoint(text, p);
    const int advance = selectGlyph(cp);
    w += advance * scale;
  }
  return w;
}
void Canvas::text(int x, int y, int w, int h, std::string_view value, uint16_t color, int scale,
                  bool centered) {
  x0_ = std::max(0, x);
  y0_ = std::max(0, y);
  x1_ = std::min(board::width, x + w);
  y1_ = std::min(board::height, y + h);
  ink_ = color;
  scale_ = std::clamp(scale, 1, 3);
  const int step = 18 * scale_;
  size_t pos = 0;
  int row = y;
  while (pos < value.size() && row + 16 * scale_ <= y1_) {
    const size_t start = pos;
    int width = 0;
    while (pos < value.size()) {
      size_t next = pos;
      auto cp = nextCodepoint(value, next);
      if (cp == '\n')
        break;
      const int advance = selectGlyph(cp);
      if (width + advance * scale_ > w)
        break;
      width += advance * scale_;
      pos = next;
    }
    if (pos == start && value[pos] != '\n')
      break;
    int left = x + (centered ? (w - width) / 2 : 0);
    size_t p = start;
    while (p < pos) {
      auto cp = nextCodepoint(value, p);
      const int advance = selectGlyph(cp);
      ox_ = left;
      oy_ = row;
      u8g2_DrawGlyph(&font_, 0, 14, cp);
      left += advance * scale_;
    }
    if (pos < value.size() && value[pos] == '\n')
      ++pos;
    row += step;
  }
  x0_ = y0_ = 0;
  x1_ = board::width;
  y1_ = board::height;
}
void Canvas::marquee(int x, int y, int w, std::string_view value, uint16_t color, uint32_t now,
                     unsigned speed, bool centered) {
  const int width = textWidth(value);
  if (width <= w || !speed) {
    text(x, y, w, 20, value, color, 1, centered);
    return;
  }
  const uint32_t travel = uint64_t(width - w) * 1000 / speed, period = travel * 2 + 4000,
                 phase = now % period;
  const int shift =
      phase < 2000            ? 0
      : phase < 2000 + travel ? uint64_t(phase - 2000) * speed / 1000
      : phase < 4000 + travel
          ? width - w
          : std::max(0, width - w - int(uint64_t(phase - 4000 - travel) * speed / 1000));
  x0_ = std::max(0, x);
  x1_ = std::min(board::width, x + w);
  y0_ = std::max(0, y);
  y1_ = std::min(board::height, y + 20);
  ink_ = color;
  scale_ = 1;
  int left = x - shift;
  size_t pos = 0;
  while (pos < value.size()) {
    auto cp = nextCodepoint(value, pos);
    const int advance = selectGlyph(cp);
    ox_ = left;
    oy_ = y;
    u8g2_DrawGlyph(&font_, 0, 14, cp);
    left += advance;
  }
  x0_ = y0_ = 0;
  x1_ = board::width;
  y1_ = board::height;
}
void Canvas::scaledImage(int x, int y, unsigned side, const uint16_t *data, unsigned inputSide) {
  if (!data || !side || !inputSide)
    return;
  if (side >= inputSide) {
    for (unsigned j = 0; j < side; ++j)
      for (unsigned i = 0; i < side; ++i)
        pixel(x + i, y + j, data[(j * inputSide / side) * inputSide + i * inputSide / side]);
    return;
  }
  // Reduction averages the covered source block so small covers stay legible.
  for (unsigned j = 0; j < side; ++j) {
    const unsigned y0 = j * inputSide / side, y1 = std::max(y0 + 1, (j + 1) * inputSide / side);
    for (unsigned i = 0; i < side; ++i) {
      const unsigned x0 = i * inputSide / side, x1 = std::max(x0 + 1, (i + 1) * inputSide / side);
      unsigned r = 0, g = 0, b = 0;
      for (unsigned sy = y0; sy < y1; ++sy)
        for (unsigned sx = x0; sx < x1; ++sx) {
          const uint16_t p = data[sy * inputSide + sx];
          r += p >> 11;
          g += (p >> 5) & 63;
          b += p & 31;
        }
      const unsigned n = (x1 - x0) * (y1 - y0);
      pixel(x + i, y + j, uint16_t((r + n / 2) / n << 11 | (g + n / 2) / n << 5 | (b + n / 2) / n));
    }
  }
}
} // namespace nova

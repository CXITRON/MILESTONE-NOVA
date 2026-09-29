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
int Canvas::textWidth(std::string_view text, int scale) {
  size_t p = 0;
  int w = 0;
  while (p < text.size()) {
    auto cp = nextCodepoint(text, p);
    if (cp > 0xFFFF)
      cp = '?';
    int advance = u8g2_GetGlyphWidth(&font_, cp);
    if (advance <= 0)
      advance = 8;
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
      int advance = cp <= 0xFFFF ? u8g2_GetGlyphWidth(&font_, cp) : 0;
      if (advance <= 0)
        advance = 8;
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
      int advance = cp <= 0xFFFF ? u8g2_GetGlyphWidth(&font_, cp) : 0;
      if (advance <= 0) {
        cp = '?';
        advance = 8;
      }
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
    int advance = cp <= 0xffff ? u8g2_GetGlyphWidth(&font_, cp) : 0;
    if (advance <= 0) {
      cp = '?';
      advance = 8;
    }
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
  for (unsigned j = 0; j < side; ++j)
    for (unsigned i = 0; i < side; ++i)
      pixel(x + i, y + j, data[(j * inputSide / side) * inputSide + i * inputSide / side]);
}
} // namespace nova

#pragma once
#include "../board/Board.h"
#ifdef ARDUINO
#include <U8g2lib.h>
#else
extern "C" {
#include <u8g2.h>
}
#endif
#include <string_view>
namespace nova {
class Canvas {
public:
  ~Canvas();
  bool begin();
  uint16_t *pixels() { return pixels_; }
  void clear(uint16_t color);
  void theme(uint16_t accent, uint16_t muted) {
    accent_ = accent;
    muted_ = muted;
  }
  uint16_t accent() const { return accent_; }
  uint16_t muted() const { return muted_; }
  void rect(int x, int y, int w, int h, uint16_t color);
  void pixel(int x, int y, uint16_t color);
  void image(int x, int y, int w, int h, const uint16_t *data, bool monochrome = false);
  void text(int x, int y, int width, int height, std::string_view value, uint16_t color,
            int scale = 1, bool centered = false);
  void marquee(int x, int y, int width, std::string_view value, uint16_t color, uint32_t now,
               unsigned speed, bool centered);
  void scaledImage(int x, int y, unsigned side, const uint16_t *data, unsigned inputSide);
  int textWidth(std::string_view value, int scale = 1);

private:
  int selectGlyph(uint32_t &codepoint);
  static void glyphLine(u8g2_t *font, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t len, uint8_t dir);
  uint16_t *pixels_ = nullptr;
  struct FontContext : u8g2_t {
    Canvas *owner = nullptr;
  };
  FontContext font_{};
  int ox_ = 0, oy_ = 0, scale_ = 1, x0_ = 0, y0_ = 0, x1_ = board::width, y1_ = board::height;
  uint16_t ink_ = 0, accent_ = 0x5EB8, muted_ = 0x7C71;
};
namespace color {
inline constexpr uint16_t background = 0x0862, panel = 0x10C4, white = 0xEF7D, muted = 0x7C71;
inline constexpr uint16_t accent = 0x5EB8, warning = 0xFD65, danger = 0xF9A7;
} // namespace color
} // namespace nova

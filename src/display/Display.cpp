#include "Display.h"
#include <algorithm>
#include <cstring>
#include <esp_heap_caps.h>
namespace nova {
void Display::command(uint8_t cmd, const uint8_t *data, size_t n) {
  spi_.beginTransaction(settings_);
  digitalWrite(board::lcdCs, LOW);
  digitalWrite(board::lcdDc, LOW);
  spi_.write(cmd);
  digitalWrite(board::lcdDc, HIGH);
  if (n)
    spi_.writeBytes(data, n);
  digitalWrite(board::lcdCs, HIGH);
  spi_.endTransaction();
}
bool Display::begin(uint32_t hz, uint8_t light, bool inverted) {
  pinMode(board::lcdCs, OUTPUT);
  pinMode(board::lcdDc, OUTPUT);
  pinMode(board::lcdReset, OUTPUT);
  digitalWrite(board::lcdCs, HIGH);
  ledcAttach(board::backlight, 5000, 8);
  brightness(0);
  spi_.begin(board::lcdSck, -1, board::lcdMosi, board::lcdCs);
  frequency(hz);
  digitalWrite(board::lcdReset, LOW);
  delay(10);
  digitalWrite(board::lcdReset, HIGH);
  delay(120);
  command(0x01);
  delay(150);
  command(0x11);
  delay(120);
  const uint8_t rgb565 = 0x55, madctl = board::lcdMadctl;
  command(0x3A, &rgb565, 1);
  command(0x36, &madctl, 1);
  command(inverted ? 0x21 : 0x20);
  command(0x13);
  command(0x29);
  sent_ = static_cast<uint16_t *>(
      heap_caps_malloc(board::width * board::height * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  ready_ = canvas_.begin() && sent_;
  if (ready_) {
    canvas_.clear(color::background);
    present();
    brightness(light);
  }
  return ready_;
}
void Display::brightness(uint8_t value) { ledcWrite(board::backlight, value); }
void Display::inversion(bool inverted) {
  if (ready_)
    command(inverted ? 0x21 : 0x20);
}
void Display::frequency(uint32_t hz) { settings_ = SPISettings(hz, MSBFIRST, SPI_MODE0); }
void Display::sleep() {
  brightness(0);
  if (ready_) {
    command(0x28);
    command(0x10);
  }
  pending_ = false;
}
void Display::present() {
  if (ready_ && !pending_) {
    strip_ = 0;
    pending_ = true;
    // Forced strips never refreshed the remembered copy; resend all once so nothing stays stale.
    if (sentStale_ && forcedFirst_ < 0) {
      initial_ = true;
      sentStale_ = false;
    }
  }
}
void Display::flush(uint32_t sliceUs) {
  if (!ready_ || !pending_)
    return;
  constexpr size_t pixels = board::width * 8;
  // Sending only one strip per loop spreads a full redraw over 40 framework yields and
  // unrelated work. Batch strips for a short slice, then return to input/network handling.
  // This reduces the visible rolling update; it is not panel TE/vblank synchronization.
  const uint32_t started = micros();
  while (strip_ < board::height / 8) {
    const unsigned index = strip_++;
    const unsigned row = index * 8;
    const size_t start = row * board::width;
    auto *current = canvas_.pixels() + start;
    const bool forced = int(index) >= forcedFirst_ && int(index) <= forcedLast_ && forcedFirst_ >= 0;
    if (!forced && !initial_ && !memcmp(current, sent_ + start, pixels * 2))
      continue;
    // Forced (video) strips are contiguous in the canvas: set one window and stream them in one
    // transaction instead of a window command and transaction per 8 rows.
    const unsigned last =
        forced ? std::min<unsigned>(unsigned(forcedLast_), board::height / 8 - 1) : index;
    const unsigned endRow = last * 8 + 7;
    const size_t bytes = size_t(last - index + 1) * pixels * 2;
    const uint8_t columns[]{0, 0, 0, 239};
    const uint8_t rows[]{uint8_t(row >> 8), uint8_t(row), uint8_t(endRow >> 8), uint8_t(endRow)};
    command(0x2A, columns, 4);
    command(0x2B, rows, 4);
    command(0x2C);
    spi_.beginTransaction(settings_);
    digitalWrite(board::lcdCs, LOW);
    digitalWrite(board::lcdDc, HIGH);
    spi_.writePixels(current, bytes);
    digitalWrite(board::lcdCs, HIGH);
    spi_.endTransaction();
    if (forced) {
      sentStale_ = true;
      strip_ = last + 1;
    } else
      memcpy(sent_ + start, current, pixels * 2);
    if (uint32_t(micros() - started) >= sliceUs)
      break;
  }
  flushUs_ += uint32_t(micros() - started);
  if (strip_ >= board::height / 8) {
    pending_ = false;
    initial_ = false;
    ++frames_;
  }
}
} // namespace nova

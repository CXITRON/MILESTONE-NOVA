#pragma once
#include "Canvas.h"
#include <SPI.h>
namespace nova {
class Display {
public:
  bool begin(uint32_t frequency, uint8_t brightness, bool inverted);
  void brightness(uint8_t value);
  void frequency(uint32_t hz);
  void inversion(bool inverted);
  void sleep();
  void present();
  void flush();
  bool busy() const { return pending_; }
  bool ready() const { return ready_; }
  Canvas &canvas() { return canvas_; }

private:
  void command(uint8_t value, const uint8_t *data = nullptr, size_t count = 0);
  SPIClass spi_{FSPI};
  SPISettings settings_{board::lcdHz, MSBFIRST, SPI_MODE0};
  Canvas canvas_;
  uint16_t *sent_ = nullptr;
  unsigned strip_ = 0;
  bool ready_ = false, pending_ = false, initial_ = true;
};
} // namespace nova

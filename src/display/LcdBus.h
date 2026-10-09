#pragma once
#include <cstddef>
#include <cstdint>
#ifdef ARDUINO
#include <Arduino.h>
#include <driver/spi_master.h>
#else
#include <SPI.h>
#endif
namespace nova {
// Owns the LCD SPI bus. Each transfer completes before returning: canvas ownership never changes.
class LcdBus {
public:
  ~LcdBus();
  bool begin(uint32_t hz);
  bool frequency(uint32_t hz);
  bool command(uint8_t value, const uint8_t *data, size_t bytes);
  bool pixels(const uint16_t *data, size_t bytes);
private:
#ifdef ARDUINO
  spi_device_handle_t device_ = nullptr;
  uint16_t *buffer_ = nullptr;
  bool bus_ = false;
#else
  SPIClass spi_{FSPI};
  SPISettings settings_{20000000, MSBFIRST, SPI_MODE0};
#endif
};
} // namespace nova

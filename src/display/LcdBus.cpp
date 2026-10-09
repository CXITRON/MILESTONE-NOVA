#include "LcdBus.h"
#include "../board/Board.h"
#include <algorithm>
#include <cstring>
#ifdef ARDUINO
#include <esp_heap_caps.h>
#endif
namespace nova {
LcdBus::~LcdBus() {
#ifdef ARDUINO
  if (device_) spi_bus_remove_device(device_);
  if (bus_) spi_bus_free(SPI2_HOST);
  free(buffer_);
#endif
}
bool LcdBus::begin(uint32_t hz) {
#ifdef ARDUINO
  constexpr size_t bytes = board::width * 8 * 2;
  buffer_ = static_cast<uint16_t *>(heap_caps_aligned_alloc(32, bytes,
                               MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  if (!buffer_) return false;
  spi_bus_config_t bus{};
  bus.mosi_io_num = board::lcdMosi; bus.miso_io_num = -1; bus.sclk_io_num = board::lcdSck;
  bus.quadwp_io_num = bus.quadhd_io_num = -1;
  bus.max_transfer_sz = bytes;
  if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;
  bus_ = true;
#else
  spi_.begin(board::lcdSck, -1, board::lcdMosi, board::lcdCs);
#endif
  return frequency(hz);
}
bool LcdBus::frequency(uint32_t hz) {
#ifdef ARDUINO
  if (!bus_) return false;
  if (device_ && spi_bus_remove_device(device_) != ESP_OK) return false;
  device_ = nullptr;
  spi_device_interface_config_t cfg{};
  cfg.clock_speed_hz = hz; cfg.mode = 0; cfg.spics_io_num = -1; cfg.queue_size = 1;
  return spi_bus_add_device(SPI2_HOST, &cfg, &device_) == ESP_OK;
#else
  settings_ = SPISettings(hz, MSBFIRST, SPI_MODE0);
  return true;
#endif
}
bool LcdBus::command(uint8_t cmd, const uint8_t *data, size_t n) {
  bool ok = true;
#ifdef ARDUINO
  if (!device_) return false;
  spi_transaction_t t{};
  t.flags = SPI_TRANS_USE_TXDATA; t.length = 8; t.tx_data[0] = cmd;
  digitalWrite(board::lcdCs, LOW); digitalWrite(board::lcdDc, LOW);
  ok = spi_device_polling_transmit(device_, &t) == ESP_OK;
  digitalWrite(board::lcdDc, HIGH);
  if (ok && n) {
    t = {}; t.length = n * 8;
    if (n <= sizeof(t.tx_data)) { t.flags = SPI_TRANS_USE_TXDATA; memcpy(t.tx_data, data, n); }
    else t.tx_buffer = data;
    ok = spi_device_transmit(device_, &t) == ESP_OK;
  }
#else
  spi_.beginTransaction(settings_);
  digitalWrite(board::lcdCs, LOW); digitalWrite(board::lcdDc, LOW);
  spi_.write(cmd); digitalWrite(board::lcdDc, HIGH);
  if (n) spi_.writeBytes(data, n);
  spi_.endTransaction();
#endif
  digitalWrite(board::lcdCs, HIGH);
  return ok;
}
bool LcdBus::pixels(const uint16_t *data, size_t n) {
  if (!data || n % 2 || (reinterpret_cast<uintptr_t>(data) & 3)) return false;
  bool ok = true;
  digitalWrite(board::lcdCs, LOW); digitalWrite(board::lcdDc, HIGH);
#ifdef ARDUINO
  if (!device_) { digitalWrite(board::lcdCs, HIGH); return false; }
  // Stage/swizzle one DMA-capable strip. No hidden driver allocation per frame or PSRAM DMA.
  constexpr size_t count = board::width * 8;
  for (size_t at = 0; at < n / 2 && ok; at += count) {
    const size_t take = std::min(count, n / 2 - at);
    // Canvas and strip offsets are word-aligned. Swap two RGB565 pixels per load/store.
    const auto *source = static_cast<const uint32_t *>(__builtin_assume_aligned(data + at, 4));
    auto *target = static_cast<uint32_t *>(__builtin_assume_aligned(buffer_, 32));
    for (size_t i = 0; i < take / 2; ++i) {
      uint32_t pair;
      __builtin_memcpy(&pair, source + i, sizeof(pair));
      const uint32_t swapped = ((pair & 0x00ff00ffu) << 8) | ((pair & 0xff00ff00u) >> 8);
      __builtin_memcpy(target + i, &swapped, sizeof(swapped));
    }
    if (take % 2) buffer_[take - 1] = __builtin_bswap16(data[at + take - 1]);
    spi_transaction_t t{};
    t.length = take * 16; t.tx_buffer = buffer_;
    ok = spi_device_transmit(device_, &t) == ESP_OK;
  }
#else
  spi_.beginTransaction(settings_); spi_.writePixels(data, n); spi_.endTransaction();
#endif
  digitalWrite(board::lcdCs, HIGH);
  return ok;
}
} // namespace nova

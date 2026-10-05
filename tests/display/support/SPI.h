#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
constexpr int FSPI = 0, MSBFIRST = 0, SPI_MODE0 = 0, OUTPUT = 0, LOW = 0, HIGH = 1;
inline uint32_t testMicros = 0, pixelWriteUs = 1000;
inline uint32_t micros() { return testMicros; }
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline void delay(unsigned) {}
inline void ledcAttach(int, int, int) {}
inline void ledcWrite(int, unsigned) {}
struct SPISettings { SPISettings(uint32_t, int, int) {} };
inline std::vector<std::vector<uint16_t>> transfers;
inline std::vector<unsigned> startRows;
inline unsigned currentCommand = 0;
struct SPIClass {
  explicit SPIClass(int) {}
  void begin(int, int, int, int) {}
  void beginTransaction(SPISettings) {}
  void endTransaction() {}
  void write(uint8_t cmd) { currentCommand = cmd; }
  void writeBytes(const uint8_t *bytes, size_t size) {
    if (currentCommand == 0x2B && size == 4) startRows.push_back(bytes[0] * 256 + bytes[1]);
  }
  void writePixels(const void *bytes, size_t size) {
    transfers.emplace_back(size / 2);
    memcpy(transfers.back().data(), bytes, size);
    testMicros += pixelWriteUs;
  }
};

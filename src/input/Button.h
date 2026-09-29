#pragma once
#include <cstdint>
namespace nova {
enum class Press : uint8_t { None, Short, Long, Repeat };
class Button {
public:
  void begin(bool pressed, uint32_t now);
  Press sample(bool pressed, uint32_t now, bool repeat = false);

private:
  bool raw_ = false, down_ = false, suppressed_ = false, longSent_ = false;
  uint32_t changed_ = 0, started_ = 0, repeated_ = 0;
};
} // namespace nova

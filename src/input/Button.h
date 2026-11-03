#pragma once
#include <cstdint>
namespace nova {
// Down/Up are raw edges with their sample time; only the memo keyer asks for them.
enum class Press : uint8_t { None, Short, Long, Repeat, Down, Up };
class Button {
public:
  void begin(bool pressed, uint32_t now);
  Press sample(bool pressed, uint32_t now, bool repeat = false);
  // Reports the last debounced edge once; `at` is when the level first changed.
  bool edge(bool &down, uint32_t &at);

private:
  bool raw_ = false, down_ = false, suppressed_ = false, longSent_ = false, edge_ = false;
  uint32_t changed_ = 0, started_ = 0, repeated_ = 0, edgeAt_ = 0;
};
} // namespace nova

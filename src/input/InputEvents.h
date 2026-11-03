#pragma once
#include "Button.h"
#include <array>
#include <cstddef>
namespace nova {
enum class Key : uint8_t { Back, Prev, Ok, Next, Menu };
struct InputEvent {
  Key key;
  Press press;
  uint32_t at = 0; // Sample time; set for Down/Up edges.
};
// Single sampler plus a delayed consumer; Buttons supplies the short critical section.
class InputEvents {
public:
  void begin(const std::array<bool, 5> &pressed, uint32_t now);
  void sample(const std::array<bool, 5> &pressed, uint32_t now);
  bool pop(InputEvent &event);
  uint32_t dropped() const { return dropped_; }
  // While on, OK reports only Down/Up edges (for Morse keying) instead of Short/Long.
  void timing(bool on) { timing_ = on; }

private:
  void push(InputEvent event);
  std::array<Button, 5> keys_{};
  std::array<InputEvent, 32> events_{};
  size_t count_ = 0;
  uint32_t dropped_ = 0;
  bool timing_ = false;
};
} // namespace nova

#pragma once
#include "Button.h"
#include <array>
#include <cstddef>
namespace nova {
enum class Key : uint8_t { Back, Prev, Ok, Next, Menu };
struct InputEvent {
  Key key;
  Press press;
};
// Single sampler plus a delayed consumer; Buttons supplies the short critical section.
class InputEvents {
public:
  void begin(const std::array<bool, 5> &pressed, uint32_t now);
  void sample(const std::array<bool, 5> &pressed, uint32_t now);
  bool pop(InputEvent &event);
  uint32_t dropped() const { return dropped_; }

private:
  void push(InputEvent event);
  std::array<Button, 5> keys_{};
  std::array<InputEvent, 32> events_{};
  size_t count_ = 0;
  uint32_t dropped_ = 0;
};
} // namespace nova

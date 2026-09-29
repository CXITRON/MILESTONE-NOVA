#pragma once
#include "../board/Board.h"
#include "Button.h"
#include <array>
namespace nova {
enum class Key : uint8_t { Back, Prev, Ok, Next, Menu };
struct InputEvent {
  Key key;
  Press press;
};
class Buttons {
public:
  void begin();
  bool poll(InputEvent &event);

private:
  std::array<Button, 5> keys_{};
  unsigned cursor_ = 0;
};
} // namespace nova

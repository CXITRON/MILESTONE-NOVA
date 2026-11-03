#include "Button.h"
namespace nova {
void Button::begin(bool pressed, uint32_t now) {
  raw_ = down_ = suppressed_ = pressed;
  longSent_ = edge_ = false;
  changed_ = started_ = repeated_ = edgeAt_ = now;
}
Press Button::sample(bool pressed, uint32_t now, bool repeat) {
  if (pressed != raw_) {
    raw_ = pressed;
    changed_ = now;
  }
  if (raw_ != down_ && now - changed_ >= 30) {
    down_ = raw_;
    // A key held since begin() reports neither edge, like it reports no press.
    edge_ = !suppressed_;
    edgeAt_ = changed_;
    if (down_) {
      started_ = now;
      longSent_ = false;
    } else {
      if (suppressed_) {
        suppressed_ = false;
        return Press::None;
      }
      if (!longSent_)
        return Press::Short;
    }
  }
  if (!down_ || suppressed_)
    return Press::None;
  if (!longSent_ && now - started_ >= 900) {
    longSent_ = true;
    repeated_ = now;
    return Press::Long;
  }
  if (repeat && longSent_ && now - repeated_ >= 180) {
    repeated_ = now;
    return Press::Repeat;
  }
  return Press::None;
}
bool Button::edge(bool &down, uint32_t &at) {
  if (!edge_)
    return false;
  edge_ = false;
  down = down_;
  at = edgeAt_;
  return true;
}
} // namespace nova

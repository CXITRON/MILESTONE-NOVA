#include "InputEvents.h"
namespace nova {
void InputEvents::begin(const std::array<bool, 5> &pressed, uint32_t now) {
  count_ = dropped_ = 0;
  for (size_t i = 0; i < keys_.size(); ++i)
    keys_[i].begin(pressed[i], now);
}
void InputEvents::sample(const std::array<bool, 5> &pressed, uint32_t now) {
  for (size_t i = 0; i < keys_.size(); ++i) {
    const auto press = keys_[i].sample(pressed[i], now, i == 1 || i == 3);
    if (press != Press::None)
      push({static_cast<Key>(i), press});
  }
}
void InputEvents::push(InputEvent event) {
  // Held PREV/NEXT must not fill the backlog and crowd out MENU/BACK/OK presses.
  if (event.press == Press::Repeat) {
    for (size_t i = 0; i < count_; ++i)
      if (events_[i].key == event.key && events_[i].press == Press::Repeat)
        return;
    if (count_ >= events_.size() / 2)
      return;
  }
  if (count_ == events_.size()) {
    size_t repeat = 0;
    while (repeat < count_ && events_[repeat].press != Press::Repeat)
      ++repeat;
    if (repeat == count_) {
      ++dropped_;
      return;
    }
    for (size_t i = repeat + 1; i < count_; ++i)
      events_[i - 1] = events_[i];
    --count_;
  }
  events_[count_++] = event;
}
bool InputEvents::pop(InputEvent &event) {
  if (!count_)
    return false;
  event = events_[0];
  for (size_t i = 1; i < count_; ++i)
    events_[i - 1] = events_[i];
  --count_;
  return true;
}
} // namespace nova

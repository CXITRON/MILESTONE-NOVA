#include "Buttons.h"
#include <Arduino.h>
#include <driver/rtc_io.h>
namespace nova {
void Buttons::begin() {
  rtc_gpio_deinit(static_cast<gpio_num_t>(board::ok));
  for (size_t n = 0; n < keys_.size(); ++n) {
    pinMode(board::buttons[n], INPUT_PULLUP);
    keys_[n].begin(digitalRead(board::buttons[n]) == LOW, millis());
  }
}
bool Buttons::poll(InputEvent &event) {
  for (unsigned n = 0; n < keys_.size(); ++n) {
    const unsigned i = cursor_++ % keys_.size();
    const auto p =
        keys_[i].sample(digitalRead(board::buttons[i]) == LOW, millis(), i == 1 || i == 3);
    if (p != Press::None) {
      event = {static_cast<Key>(i), p};
      return true;
    }
  }
  return false;
}
} // namespace nova

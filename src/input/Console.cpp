#include "Console.h"
#include <Arduino.h>
namespace nova {
const char *Console::poll() {
  if (ready_) {
    used_ = 0;
    ready_ = false;
  }
  for (unsigned i = 0; i < 32 && Serial.available(); ++i) {
    const int c = Serial.read();
    if (c == '\r')
      continue;
    if (c == '\n') {
      if (overflow_) {
        overflow_ = false;
        used_ = 0;
        continue;
      }
      line_[used_] = 0;
      ready_ = true;
      return line_;
    }
    if (used_ + 1 < sizeof(line_))
      line_[used_++] = c;
    else
      overflow_ = true;
  }
  return nullptr;
}
} // namespace nova

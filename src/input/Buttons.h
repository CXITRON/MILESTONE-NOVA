#pragma once
#include "../board/Board.h"
#include "InputEvents.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
namespace nova {
class Buttons {
public:
  bool begin();
  bool poll(InputEvent &event);
  uint32_t dropped();

private:
  static void task(void *context);
  void sample();
  InputEvents events_;
  TaskHandle_t sampler_ = nullptr;
  portMUX_TYPE gate_ = portMUX_INITIALIZER_UNLOCKED;
};
} // namespace nova

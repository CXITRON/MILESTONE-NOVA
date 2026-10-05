#include "Buttons.h"
#include <Arduino.h>
#include <driver/rtc_io.h>
namespace nova {
namespace {
std::array<bool, 5> levels() {
  std::array<bool, 5> pressed{};
  for (size_t i = 0; i < pressed.size(); ++i)
    pressed[i] = digitalRead(board::buttons[i]) == LOW;
  return pressed;
}
} // namespace
bool Buttons::begin() {
  rtc_gpio_deinit(static_cast<gpio_num_t>(board::ok));
  for (int pin : board::buttons)
    pinMode(pin, INPUT_PULLUP);
  events_.begin(levels(), millis());
  return xTaskCreate(task, "nova-input", 2048, this, 2, &sampler_) == pdPASS;
}
void Buttons::sample() {
  const auto pressed = levels();
  const auto now = millis();
  portENTER_CRITICAL(&gate_);
  events_.sample(pressed, now);
  portEXIT_CRITICAL(&gate_);
}
void Buttons::task(void *context) {
  auto &self = *static_cast<Buttons *>(context);
  TickType_t wake = xTaskGetTickCount();
  const TickType_t period = pdMS_TO_TICKS(5) ? pdMS_TO_TICKS(5) : 1;
  for (;;) {
    self.sample();
    vTaskDelayUntil(&wake, period);
  }
}
bool Buttons::poll(InputEvent &event) {
  if (!sampler_)
    sample(); // Allocation failure retains the previous synchronous behavior.
  portENTER_CRITICAL(&gate_);
  const bool available = events_.pop(event);
  portEXIT_CRITICAL(&gate_);
  return available;
}
uint32_t Buttons::dropped() {
  portENTER_CRITICAL(&gate_);
  const auto count = events_.dropped();
  portEXIT_CRITICAL(&gate_);
  return count;
}
} // namespace nova

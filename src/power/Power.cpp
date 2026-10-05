#include "Power.h"
#include "../board/Board.h"
#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
namespace nova {
bool Power::request(uint32_t now, bool updating) {
  if (updating || state_ != State::Awake)
    return false;
  state_ = State::Stopping;
  since_ = now;
  return true;
}
void Power::peripheralsStopped(uint32_t now) {
  state_ = State::Release;
  released_ = now;
}
void Power::tick(uint32_t now) {
  if (state_ != State::Release)
    return;
  if (digitalRead(board::ok) == LOW) {
    released_ = now;
    return;
  }
  if (now - released_ < 80)
    return;
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  const auto pin = static_cast<gpio_num_t>(board::ok);
  rtc_gpio_pullup_en(pin);
  rtc_gpio_pulldown_dis(pin);
  if (esp_sleep_enable_ext0_wakeup(pin, 0) != ESP_OK) {
    state_ = State::Failed;
    return;
  }
  for (int p : {board::backlight, board::rgb}) {
    ledcDetach(p);
    pinMode(p, OUTPUT);
    digitalWrite(p, LOW);
    gpio_hold_en(static_cast<gpio_num_t>(p));
  }
  gpio_deep_sleep_hold_en();
  esp_deep_sleep_start();
}
} // namespace nova

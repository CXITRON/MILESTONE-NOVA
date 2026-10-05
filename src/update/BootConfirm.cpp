#include "BootConfirm.h"
#include "../logging/Log.h"
#include <Arduino.h>
#include <esp_ota_ops.h>
namespace nova {
void BootConfirm::begin() {
  esp_ota_img_states_t state;
  pending_ = esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
             state == ESP_OTA_IMG_PENDING_VERIFY;
  started_ = millis();
  loops_ = 0;
  if (pending_)
    log("UPDATE", "pending image; 60-second runtime confirmation required");
}
void BootConfirm::tick(uint32_t now) {
  if (!pending_)
    return;
  ++loops_;
  if (now - started_ < 60000 || loops_ < 500 || ESP.getFreeHeap() < 16384)
    return;
  if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
    pending_ = false;
    log("UPDATE", "runtime confirmed; rollback cancelled");
  } else {
    started_ = now;
    log("UPDATE", "confirmation failed; retry in 60 seconds");
  }
}
} // namespace nova

#pragma once
#include "Runtime.h"
#include "esp_app_desc.h"
inline constexpr int ESP_OK = 0;
using esp_ota_img_states_t = int;
inline constexpr int ESP_OTA_IMG_PENDING_VERIFY = 1;
inline constexpr int ESP_OTA_IMG_VALID = 2;
inline int testBootState = 0, testStateResult = ESP_OK, testConfirmResult = ESP_OK;
inline unsigned testConfirmCalls = 0;
inline const void *esp_ota_get_running_partition() { return nullptr; }
inline int esp_ota_get_state_partition(const void *, int *state) {
  *state = testBootState;
  return testStateResult;
}
inline int esp_ota_mark_app_valid_cancel_rollback() {
  ++testConfirmCalls;
  return testConfirmResult;
}
inline const void *esp_ota_get_next_update_partition(const void *) { return nullptr; }
inline int esp_ota_get_partition_description(const void *, esp_app_desc_t *) { return -1; }
inline int esp_ota_set_boot_partition(const void *) { return -1; }

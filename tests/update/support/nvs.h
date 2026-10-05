#pragma once
#include <cstdint>
// Host stand-in for ESP-IDF NVS statistics; the settings store only reads them after a failed save.
struct nvs_stats_t {
  uint32_t used_entries = 0, free_entries = 0, available_entries = 0, total_entries = 0, namespace_count = 0;
};
constexpr int ESP_OK = 0;
inline int nvs_get_stats(const char *, nvs_stats_t *stats) {
  *stats = nvs_stats_t{};
  return ESP_OK;
}

#include "Preferences.h"
using esp_err_t = int;
using nvs_handle_t = std::string;
constexpr int NVS_READWRITE = 1, ESP_FAIL = -1, ESP_ERR_NO_MEM = 0x101,
              ESP_ERR_INVALID_STATE = 0x103, ESP_ERR_NVS_NOT_ENOUGH_SPACE = 0x1105;
inline esp_err_t nvs_open(const char *space, int, nvs_handle_t *handle) {
  *handle = space;
  return ESP_OK;
}
inline esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t size) {
  if (testNvsWriteFail) return ESP_FAIL;
  if (testNvsCapacity && testNvsBytes() - testNvs[handle][key].size() + size > testNvsCapacity)
    return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
  Preferences p;
  p.begin(handle.c_str());
  return p.putBytes(key, data, size) == size ? ESP_OK : ESP_FAIL;
}
inline esp_err_t nvs_commit(nvs_handle_t) { return ESP_OK; }
inline void nvs_close(nvs_handle_t) {}
inline const char *esp_err_to_name(esp_err_t error) {
  switch (error) {
  case ESP_ERR_NO_MEM: return "ESP_ERR_NO_MEM";
  case ESP_ERR_NVS_NOT_ENOUGH_SPACE: return "ESP_ERR_NVS_NOT_ENOUGH_SPACE";
  case ESP_ERR_INVALID_STATE: return "ESP_ERR_INVALID_STATE";
  default: return "ESP_FAIL";
  }
}

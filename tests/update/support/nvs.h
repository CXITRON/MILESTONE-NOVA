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

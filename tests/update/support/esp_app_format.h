#pragma once
#include <cstdint>
inline constexpr uint8_t ESP_IMAGE_HEADER_MAGIC = 0xe9;
inline constexpr uint16_t ESP_CHIP_ID_ESP32S3 = 9;
struct esp_image_header_t {
  uint8_t magic = 0, remainder[11]{};
  uint16_t chip_id = 0;
  uint8_t end[10]{};
};
struct esp_image_segment_header_t { uint32_t load_addr = 0, data_len = 0; };
static_assert(sizeof(esp_image_header_t) == 24);

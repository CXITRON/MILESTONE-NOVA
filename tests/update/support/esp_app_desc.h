#pragma once
#include <cstdint>
inline constexpr uint32_t ESP_APP_DESC_MAGIC_WORD = 0xabcd5432;
struct esp_app_desc_t {
  uint32_t magic_word = 0, secure_version = 0, reserved[2]{};
  char version[32]{}, project_name[32]{}, remainder[176]{};
};
static_assert(sizeof(esp_app_desc_t) == 256);

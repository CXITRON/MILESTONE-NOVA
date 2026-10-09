#pragma once
#include <cstdint>
using portMUX_TYPE = int;
inline constexpr int portMUX_INITIALIZER_UNLOCKED = 0, pdTRUE = 1;
inline constexpr uint32_t portMAX_DELAY = 0xffffffff;
inline unsigned testTaskDelays = 0;
inline void vTaskDelay(unsigned) { ++testTaskDelays; }

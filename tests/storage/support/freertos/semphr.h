#pragma once
using SemaphoreHandle_t = void *;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return reinterpret_cast<void *>(1); }
inline int xSemaphoreTake(void *, unsigned) { return 1; }
inline void xSemaphoreGive(void *) {}

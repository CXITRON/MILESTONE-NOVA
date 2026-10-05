#pragma once
#include <cstdint>
#include <functional>
inline uint32_t testMillis = 0, testHeap = 65536;
inline bool testTaskCreated = true;
inline void (*testTask)(void *) = nullptr;
inline void *testContext = nullptr;
inline std::function<void()> testStep;
inline uint32_t millis() { return testMillis; }
inline uint32_t esp_random() { return 7; }
inline constexpr int pdPASS = 1;
inline unsigned pdMS_TO_TICKS(unsigned ms) { return ms; }
inline int xTaskCreate(void (*task)(void *), const char *, unsigned, void *context, unsigned, void *) {
  testTask = task;
  testContext = context;
  return testTaskCreated ? pdPASS : 0;
}
inline void vTaskDelay(unsigned ms) {
  testMillis += ms;
  if (testStep)
    testStep();
}
struct TestEsp {
  unsigned getFreeHeap() const { return testHeap; }
};
inline TestEsp ESP;

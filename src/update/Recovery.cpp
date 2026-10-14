#include "Recovery.h"
#include <algorithm>
#include <cstring>
#ifdef ARDUINO
#include <esp_attr.h>
#include <freertos/FreeRTOS.h>
#else
#define RTC_NOINIT_ATTR
#endif
namespace nova {
namespace {
struct Retained {
  uint32_t magic, failures, armed, latch;
  char operation[128], quarantine[128];
  uint32_t check;
};
RTC_NOINIT_ATTR Retained retained;
#ifdef ARDUINO
portMUX_TYPE gate = portMUX_INITIALIZER_UNLOCKED;
struct Lock { Lock() { portENTER_CRITICAL(&gate); } ~Lock() { portEXIT_CRITICAL(&gate); } };
#else
struct Lock { ~Lock() {} };
#endif
constexpr uint32_t magic = 0x4e525331;
uint32_t checksum() {
  uint32_t hash = 2166136261U;
  const auto *bytes = reinterpret_cast<const uint8_t *>(&retained);
  for (size_t i = 0; i < offsetof(Retained, check); ++i)
    hash = (hash ^ bytes[i]) * 16777619U;
  return hash;
}
void seal() { retained.check = checksum(); }
void ensure() {
  if (retained.magic == magic && retained.check == checksum() && retained.failures <= 3 &&
      retained.armed <= 1 && retained.latch <= 1 &&
      memchr(retained.operation, 0, sizeof(retained.operation)) &&
      memchr(retained.quarantine, 0, sizeof(retained.quarantine)))
    return;
  memset(&retained, 0, sizeof(retained));
  retained.magic = magic;
  seal();
}
void copy(char *out, size_t cap, const char *in) {
  if (!cap) return;
  const size_t n = std::min(cap - 1, strlen(in));
  memcpy(out, in, n);
  out[n] = 0;
}
} // namespace
void Recovery::begin(uint32_t now, bool manual, bool crashReset, bool coldReset) {
  Lock lock;
  if (coldReset) memset(&retained, 0, sizeof(retained));
  ensure();
  if (crashReset && retained.armed && retained.operation[0])
    copy(retained.quarantine, sizeof(retained.quarantine), retained.operation);
  if (crashReset && retained.armed)
    retained.failures = std::min<uint32_t>(retained.failures + 1, 3);
  else if (!crashReset && !retained.latch)
    retained.failures = 0;
  if (retained.failures >= 3) retained.latch = 1;
  active_ = manual || retained.latch;
  failures_ = retained.failures;
  // Recovery runtime alone must not certify a normal boot, or clear automatic recovery.
  retained.armed = active_ ? 0 : 1;
  retained.operation[0] = 0;
  seal();
  started_ = now;
  loops_ = 0;
  completed_ = false;
}
void Recovery::tick(uint32_t now) {
  if (active_ || completed_) return;
  if (loops_ < 500) ++loops_;
  if (uint32_t(now - started_) < 60000 || loops_ < 500) return;
  Lock lock;
  ensure();
  retained.failures = retained.armed = 0;
  seal();
  completed_ = true;
}
void Recovery::resumeNormal() {
  Lock lock;
  ensure();
  retained.failures = retained.armed = retained.latch = 0;
  retained.operation[0] = retained.quarantine[0] = 0;
  seal();
}
bool Recovery::blocked(const char *path) {
  if (!path) return false;
  Lock lock;
  ensure();
  const size_t n = strlen(retained.quarantine);
  return n && !strncmp(path, retained.quarantine, n) && (path[n] == 0 || path[n] == '/');
}
void Recovery::suspect(char *out, size_t cap) {
  Lock lock;
  ensure();
  copy(out, cap, retained.quarantine);
}
Recovery::FileScope::FileScope(const char *path) {
  Lock lock;
  ensure();
  copy(previous_, sizeof(previous_), retained.operation);
  if (path && path[0] == '/' && strlen(path) < sizeof(retained.operation))
    copy(retained.operation, sizeof(retained.operation), path);
  seal();
}
Recovery::FileScope::~FileScope() {
  Lock lock;
  ensure();
  copy(retained.operation, sizeof(retained.operation), previous_);
  seal();
}
} // namespace nova

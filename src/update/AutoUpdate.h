#pragma once
#include "Release.h"
namespace nova {
// App-owned scheduler: one release check shortly after boot, then once a day (hourly after a
// failed check). A found release is only announced unless automatic install is enabled, in which
// case it continues through download and install. Transport, verification and flash stay in
// Firmware's worker.
class AutoUpdate {
public:
  enum class Action : uint8_t { None, Check, Download, Install };
  static constexpr uint32_t settleMs = 30000, dailyMs = 86400000U, retryMs = 3600000U;
  void begin(uint32_t now) {
    last_ = now;
    interval_ = settleMs;
  }
  Action next(uint32_t now, bool autoInstall, bool permitted, bool busy, ReleaseState state);

private:
  Action pending_ = Action::None;
  uint32_t last_ = 0, interval_ = settleMs;
};
} // namespace nova

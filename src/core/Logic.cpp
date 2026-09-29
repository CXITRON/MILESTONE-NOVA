#include "Logic.h"
#include <algorithm>
#include <cmath>
namespace nova {
int thermalLevel(float temperature, int previous, float warn, float throttle, float stop) {
  if (!std::isfinite(temperature))
    return std::max(previous, 2);
  if (temperature >= stop || (previous >= 3 && temperature >= stop - 5))
    return 3;
  if (temperature >= throttle || (previous >= 2 && temperature >= throttle - 5))
    return 2;
  if (temperature >= warn || (previous >= 1 && temperature >= warn - 5))
    return 1;
  return 0;
}
bool validDate(int y, int m, int d) {
  static constexpr int days[]{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (y < 2000 || y > 2199 || m < 1 || m > 12 || d < 1)
    return false;
  return d <= days[m - 1] + (m == 2 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
}
int32_t civilDay(int y, int m, int d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yo = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  return era * 146097 + static_cast<int>(yo * 365 + yo / 4 - yo / 100 + doy) - 719468;
}
float batteryPercent(float v) {
  static constexpr float volts[]{3.20f, 3.40f, 3.55f, 3.65f, 3.72f,
                                 3.78f, 3.85f, 3.95f, 4.05f, 4.20f};
  static constexpr float pct[]{0, 3, 8, 15, 25, 40, 60, 78, 90, 100};
  if (!std::isfinite(v) || v <= volts[0])
    return 0;
  for (unsigned i = 1; i < 10; ++i)
    if (v < volts[i])
      return pct[i - 1] + (v - volts[i - 1]) * (pct[i] - pct[i - 1]) / (volts[i] - volts[i - 1]);
  return 100;
}
void FocusTimer::reset(uint32_t seconds) {
  remainingMs_ = std::clamp<uint32_t>(seconds, 60, 14400) * 1000;
  state_ = State::Idle;
}
uint32_t FocusTimer::remaining(uint32_t now) const {
  const uint32_t used = state_ == State::Running ? now - anchor_ : 0;
  return used >= remainingMs_ ? 0 : remainingMs_ - used;
}
void FocusTimer::tick(uint32_t now) {
  if (state_ == State::Running && !remaining(now)) {
    remainingMs_ = 0;
    state_ = State::Finished;
  }
}
void FocusTimer::toggle(uint32_t now) {
  tick(now);
  if (state_ == State::Running) {
    remainingMs_ = remaining(now);
    state_ = State::Paused;
  } else if (state_ != State::Finished) {
    anchor_ = now;
    state_ = State::Running;
  }
}
} // namespace nova

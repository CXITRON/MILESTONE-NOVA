#pragma once
#include <cstdint>
namespace nova {
bool validDate(int year, int month, int day);
int32_t civilDay(int year, int month, int day);
float batteryPercent(float volts);
int thermalLevel(float temperature, int previous, float warn, float throttle, float stop);
class FocusTimer {
public:
  enum class State { Idle, Running, Paused, Finished };
  void reset(uint32_t seconds);
  void toggle(uint32_t now);
  void tick(uint32_t now);
  uint32_t remaining(uint32_t now) const;
  State state() const { return state_; }

private:
  uint32_t remainingMs_ = 25 * 60000, anchor_ = 0;
  State state_ = State::Idle;
};
} // namespace nova

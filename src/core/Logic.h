#pragma once
#include <cstdint>
namespace nova {
bool validDate(int year, int month, int day);
int32_t civilDay(int year, int month, int day);
float batteryPercent(float volts);
int thermalLevel(float temperature, int previous, float warn, float throttle, float stop);
// CSAT (수능) day schedule. Times follow the 2026/2027 school-year timetable: exam periods from the
// official plan, warning/ready bells (예비령/준비령) as published in exam guides.
struct ExamStatus {
  const char *period = "";  // "1교시", "쉬는 시간" ...
  const char *subject = ""; // Large text: "국어", "점심" ...
  const char *state = "";   // What is happening now: "예비령 · 답안지 배부" ...
  const char *next = "";    // Bell or step that starts the next stage; empty after the last bell.
  int nextAt = -1;          // Seconds since midnight of that next stage, -1 when none.
  int examStart = -1, examEnd = -1; // Current answering block while testing, else -1.
  bool testing = false, closing = false, finished = false; // closing: last 10 minutes.
};
// secondsOfDay: local time 0..86399. Without the 5th period (제2외국어/한문) the day ends with
// the 4th-period bell at 16:37.
ExamStatus examStatus(int secondsOfDay, bool secondLanguage = true);
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

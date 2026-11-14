#include "Logic.h"
#include <cstring>
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
namespace {
struct ExamSlot {
  int start; // Minutes since midnight.
  const char *period, *subject, *state, *bell; // bell: what starts this slot.
  uint8_t kind; // 0 waiting, 1 answering, 2 last 10 minutes, 3 finished.
};
constexpr int hm(int h, int m) { return h * 60 + m; }
constexpr ExamSlot examSlots[]{
    {0, "수능", "입실 전", "08:10까지 입실 완료", "", 0},
    {hm(8, 10), "1교시", "국어", "입실 완료 · 대기", "입실 완료", 0},
    {hm(8, 25), "1교시", "국어", "예비령 · 답안지 배부", "예비령", 0},
    {hm(8, 35), "1교시", "국어", "준비령 · 문제지 배부", "준비령", 0},
    {hm(8, 40), "1교시", "국어", "본령 · 시험 중", "본령", 1},
    {hm(9, 50), "1교시", "국어", "종료 10분 전", "종료 10분 전 예령", 2},
    {hm(10, 0), "쉬는 시간", "휴식", "다음 2교시 수학", "종료령", 0},
    {hm(10, 20), "2교시", "수학", "예비령 · 답안지 배부", "예비령", 0},
    {hm(10, 25), "2교시", "수학", "준비령 · 문제지 배부", "준비령", 0},
    {hm(10, 30), "2교시", "수학", "본령 · 시험 중", "본령", 1},
    {hm(12, 0), "2교시", "수학", "종료 10분 전", "종료 10분 전 예령", 2},
    {hm(12, 10), "점심 시간", "점심", "다음 3교시 영어", "종료령", 0},
    {hm(13, 0), "3교시", "영어", "예비령 · 답안지 배부", "예비령", 0},
    {hm(13, 5), "3교시", "영어", "준비령 · 문제지 배부", "준비령", 0},
    {hm(13, 7), "3교시", "영어", "듣기 음질 테스트", "듣기 음질 테스트", 0},
    {hm(13, 10), "3교시", "영어", "듣기 방송 · 시험 중", "듣기 방송 시작", 1},
    {hm(14, 10), "3교시", "영어", "종료 10분 전", "종료 10분 전 예령", 2},
    {hm(14, 20), "쉬는 시간", "휴식", "다음 4교시 한국사", "종료령", 0},
    {hm(14, 40), "4교시", "한국사", "예비령 · 답안지 배부", "예비령", 0},
    {hm(14, 45), "4교시", "한국사", "준비령 · 문제지 배부", "준비령", 0},
    {hm(14, 50), "4교시", "한국사", "본령 · 시험 중", "본령", 1},
    {hm(15, 10), "4교시", "한국사", "종료 10분 전", "종료 10분 전 예령", 2},
    {hm(15, 20), "4교시", "탐구", "한국사 문답지 회수", "종료령", 0},
    {hm(15, 25), "4교시", "탐구", "예비령 · 답안지 배부", "예비령", 0},
    {hm(15, 30), "4교시", "탐구", "준비령 · 문제지 배부", "준비령", 0},
    {hm(15, 35), "4교시 제1선택", "탐구 1", "본령 · 시험 중", "본령", 1},
    {hm(15, 55), "4교시 제1선택", "탐구 1", "종료 10분 전", "종료 10분 전 예령", 2},
    {hm(16, 5), "4교시", "탐구", "제1선택 문제지 회수", "종료령", 0},
    {hm(16, 7), "4교시 제2선택", "탐구 2", "시험 중", "제2선택 시작", 1},
    {hm(16, 27), "4교시 제2선택", "탐구 2", "종료 10분 전", "종료 10분 전 예령", 2},
    {hm(16, 37), "쉬는 시간", "휴식", "다음 5교시 제2외국어/한문", "종료령", 0},
    {hm(16, 55), "5교시 제2외국어/한문", "제2외국어", "예비령 · 답안지 배부", "예비령", 0},
    {hm(17, 0), "5교시 제2외국어/한문", "제2외국어", "준비령 · 문제지 배부", "준비령", 0},
    {hm(17, 5), "5교시 제2외국어/한문", "제2외국어", "본령 · 시험 중", "본령", 1},
    {hm(17, 35), "5교시 제2외국어/한문", "제2외국어", "종료 10분 전", "종료 10분 전 예령", 2},
    {hm(17, 45), "수능", "시험 종료", "수고하셨습니다", "종료령", 3},
};
constexpr unsigned examSlotCount = sizeof(examSlots) / sizeof(*examSlots);
// Without the 5th period the slots from this {16:37 쉬는 시간} row on are replaced by the final one.
constexpr unsigned examFifth = 30;
static_assert(examSlots[examFifth].start == hm(16, 37) && examSlots[examFifth].kind == 0);
} // namespace
ExamStatus examStatus(int seconds, bool secondLanguage) {
  seconds = std::clamp(seconds, 0, 86399);
  constexpr unsigned fifth = examFifth;
  const unsigned count = secondLanguage ? examSlotCount : fifth + 1;
  unsigned at = 0;
  while (at + 1 < count && examSlots[at + 1].start * 60 <= seconds)
    ++at;
  ExamSlot slot = examSlots[at];
  if (!secondLanguage && at == fifth) {
    const ExamSlot &last = examSlots[examSlotCount - 1];
    slot = {slot.start, last.period, last.subject, last.state, slot.bell, last.kind};
  }
  ExamStatus status;
  status.period = slot.period;
  status.subject = slot.subject;
  status.state = slot.state;
  status.testing = slot.kind == 1 || slot.kind == 2;
  status.closing = slot.kind == 2;
  status.finished = slot.kind == 3;
  status.warning = !strcmp(slot.bell, "예비령");
  status.ready = !strcmp(slot.bell, "준비령") || !strcmp(slot.bell, "듣기 음질 테스트");
  if (at + 1 < count) {
    status.next = examSlots[at + 1].bell;
    status.nextAt = examSlots[at + 1].start * 60;
  }
  if (status.testing) {
    unsigned first = at, last = at;
    while (first > 0 && (examSlots[first - 1].kind == 1 || examSlots[first - 1].kind == 2))
      --first;
    while (last + 1 < count &&
           (examSlots[last + 1].kind == 1 || examSlots[last + 1].kind == 2))
      ++last;
    status.examStart = examSlots[first].start * 60;
    status.examEnd = last + 1 < count ? examSlots[last + 1].start * 60 : -1;
  }
  return status;
}
int examSkipTarget(int seconds, bool secondLanguage) {
  seconds = std::clamp(seconds, 0, 86399);
  const ExamStatus now = examStatus(seconds, secondLanguage);
  if (now.testing || now.finished)
    return -1;
  const unsigned count = secondLanguage ? examSlotCount : examFifth + 1;
  for (unsigned i = 0; i < count; ++i)
    if (examSlots[i].kind == 1 && examSlots[i].start * 60 > seconds)
      return examSlots[i].start * 60;
  return -1;
}
int examStepTarget(int seconds, int direction, bool secondLanguage) {
  seconds = std::clamp(seconds, 0, 86399);
  if (direction > 0)
    return examStatus(seconds, secondLanguage).nextAt;
  const unsigned count = secondLanguage ? examSlotCount : examFifth + 1;
  unsigned at = 0;
  while (at + 1 < count && examSlots[at + 1].start * 60 <= seconds)
    ++at;
  // Like a music player: a few seconds into a stage, PREV goes to the stage before it.
  int target = examSlots[at].start * 60;
  if (seconds - target <= 5 && at > 0)
    target = examSlots[at - 1].start * 60;
  target = std::max(target, examPracticeOrigin);
  return target < seconds ? target : -1;
}
int64_t examPracticeStartFor(int64_t now, int clockSeconds, int bellMinute) {
  int64_t diff = int64_t(bellMinute) * 60 - clockSeconds;
  if (diff > 43200)
    diff -= 86400;
  else if (diff <= -43200)
    diff += 86400;
  return now + diff - examPracticeBellOffset;
}
int examPracticeSeconds(int64_t now, int64_t start) {
  return int(std::clamp<int64_t>(examPracticeOrigin + (now - start), 0, 86399));
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

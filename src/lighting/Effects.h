#pragma once
#include "../board/Board.h"
#include <array>
namespace nova {
struct LightColor { uint8_t r = 0, g = 0, b = 0; };
using LightFrame = std::array<LightColor, board::rgbCount>;
// CSAT timetable stage while its screen is open (real or practice run).
enum class ExamLight : uint8_t { None, Waiting, Warning, Ready, Testing, Closing, Finished };
struct LightState {
  bool sleeping = false, critical = false, warning = false, updating = false;
  bool ap = false, synchronized = false, stale = false;
  bool media = false, playing = false, now = false, connected = false;
  bool timerRunning = false, timerPaused = false, timerFinished = false;
  bool track = false, artLoading = false, artValid = false, artFailed = false;
  bool core = false, dday = false;  // CORE profile, and its D-day screen.
  uint32_t trackGeneration = 0;
  unsigned progress = 0;
  ExamLight exam = ExamLight::None;
  unsigned examLeft = 0;   // Remaining answering time in percent while testing.
  int32_t examMark = 0;    // Changes with every stage (bell); a change flashes the strip.
};
enum class LightEffect : uint8_t {
  Off, Critical, Warning, Update, Boot, SyncLost, Ap, TimerDone,
  ArtMissing, ArtReady, ArtLoading, Sync, Media, Music, Paused,
  Connecting, Timer, TimerPaused, Clock, DDay, Idle,
  ExamBell, ExamWaiting, ExamWarning, ExamReady, ExamTesting, ExamClosing, ExamFinished
};
// Pure, allocation-free state/animation logic. Physical LED order belongs to Rgb.
class LightEffects {
public:
  LightFrame render(uint32_t now, uint8_t limit, const LightState &state);
  LightEffect effect() const { return effect_; }
private:
  bool initialized_ = false, bootDone_ = false, trackSeen_ = false;
  bool failedSeen_ = false, readySeen_ = false, loadingSeen_ = false;
  bool feedback_ = false, examSeen_ = false;
  int32_t examMark_ = 0;
  uint32_t examBellAt_ = 0;
  uint32_t bootAt_ = 0, track_ = 0, loadingAt_ = 0, feedbackAt_ = 0;
  LightEffect feedbackEffect_ = LightEffect::ArtMissing, effect_ = LightEffect::Off;
};
} // namespace nova

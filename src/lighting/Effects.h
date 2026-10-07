#pragma once
#include "../board/Board.h"
#include <array>
namespace nova {
struct LightColor { uint8_t r = 0, g = 0, b = 0; };
using LightFrame = std::array<LightColor, board::rgbCount>;
struct LightState {
  bool sleeping = false, critical = false, warning = false, updating = false;
  bool ap = false, synchronized = false, stale = false;
  bool media = false, playing = false, now = false, connected = false;
  bool timerRunning = false, timerPaused = false, timerFinished = false;
  bool track = false, artLoading = false, artValid = false, artFailed = false;
  bool core = false, dday = false;  // CORE profile, and its D-day screen.
  uint32_t trackGeneration = 0;
  unsigned progress = 0;
};
enum class LightEffect : uint8_t {
  Off, Critical, Warning, Update, Boot, SyncLost, Ap, TimerDone,
  ArtMissing, ArtReady, ArtLoading, Sync, Media, Music, Paused,
  Connecting, Timer, TimerPaused, Clock, DDay, Idle
};
// Error diffusion at the physical frame rate. Independent phase per LED/channel avoids
// making all five LEDs pulse in unison. Targets and each emitted value stay below limit.
class LightDither {
public:
  void reset();
  uint8_t channel(unsigned index, float target, uint8_t limit);
private:
  std::array<float, board::rgbCount * 3> error_{};
};
// Pure, allocation-free state/animation logic. Physical LED order belongs to Rgb.
class LightEffects {
public:
  LightFrame render(uint32_t now, uint8_t limit, const LightState &state, bool smooth = true);
  LightEffect effect() const { return effect_; }
  void resetOutput() { dither_.reset(); }
private:
  LightDither dither_;
  uint8_t lastLimit_ = 0;
  bool lastSmooth_ = true;
  bool initialized_ = false, bootDone_ = false, trackSeen_ = false;
  bool failedSeen_ = false, readySeen_ = false, loadingSeen_ = false;
  bool feedback_ = false;
  uint32_t bootAt_ = 0, track_ = 0, loadingAt_ = 0, feedbackAt_ = 0;
  LightEffect feedbackEffect_ = LightEffect::ArtMissing, effect_ = LightEffect::Off;
};
} // namespace nova

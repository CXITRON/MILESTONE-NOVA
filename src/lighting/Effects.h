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
  uint32_t trackGeneration = 0;
  unsigned progress = 0;
};
enum class LightEffect : uint8_t {
  Off, Critical, Warning, Update, Boot, SyncLost, Ap, TimerDone,
  ArtMissing, ArtReady, ArtLoading, Sync, Media, Music, Paused,
  Connecting, Timer, TimerPaused, Idle
};
// Pure, allocation-free state/animation logic. Physical LED order belongs to Rgb.
class LightEffects {
public:
  LightFrame render(uint32_t now, uint8_t limit, const LightState &state);
  LightEffect effect() const { return effect_; }
private:
  bool initialized_ = false, bootDone_ = false, trackSeen_ = false;
  bool failedSeen_ = false, readySeen_ = false, loadingSeen_ = false;
  bool feedback_ = false;
  uint32_t bootAt_ = 0, track_ = 0, loadingAt_ = 0, feedbackAt_ = 0;
  LightEffect feedbackEffect_ = LightEffect::ArtMissing, effect_ = LightEffect::Off;
};
} // namespace nova

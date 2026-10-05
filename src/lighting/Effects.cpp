#include "Effects.h"
#include <algorithm>
#include <cmath>
namespace nova {
LightFrame LightEffects::render(uint32_t now, uint8_t limit, const LightState &s) {
  if (!initialized_) { initialized_ = true; bootAt_ = now; }
  if (now - bootAt_ >= 1800) bootDone_ = true;
  if (!s.track || !trackSeen_ || track_ != s.trackGeneration) {
    trackSeen_ = s.track; track_ = s.trackGeneration;
    failedSeen_ = readySeen_ = loadingSeen_ = feedback_ = false;
  }
  if (s.track) {
    if (s.artLoading && !loadingSeen_) { loadingSeen_ = true; loadingAt_ = now; }
    // Consume notifications even while a higher-priority effect hides them.
    if (s.artValid && !readySeen_) {
      readySeen_ = true; feedback_ = true; feedbackAt_ = now;
      feedbackEffect_ = LightEffect::ArtReady;
    } else if (s.artFailed && !s.artValid && !failedSeen_) {
      failedSeen_ = true; feedback_ = true; feedbackAt_ = now;
      feedbackEffect_ = LightEffect::ArtMissing;
    }
  }
  if (feedback_ && now - feedbackAt_ >=
      (feedbackEffect_ == LightEffect::ArtReady ? 700u : 1200u)) feedback_ = false;
  if (s.sleeping || !limit) effect_ = LightEffect::Off;
  else if (s.critical) effect_ = LightEffect::Critical;
  else if (s.updating) effect_ = LightEffect::Update;
  else if (s.warning) effect_ = LightEffect::Warning;
  else if (!bootDone_) effect_ = LightEffect::Boot;
  else if (s.synchronized && s.stale) effect_ = LightEffect::SyncLost;
  else if (s.ap && !s.synchronized) effect_ = LightEffect::Ap;
  else if (s.timerFinished) effect_ = LightEffect::TimerDone;
  else if (s.now && s.connected && !s.media && !s.synchronized && feedback_)
    effect_ = feedbackEffect_;
  else if (s.now && s.connected && !s.media && !s.synchronized && s.track && s.artLoading && !s.artValid && !failedSeen_ &&
           loadingSeen_ && now - loadingAt_ < 8000) effect_ = LightEffect::ArtLoading;
  else if (s.synchronized) effect_ = s.playing ? LightEffect::Sync : LightEffect::Paused;
  else if (s.media) effect_ = s.playing ? LightEffect::Media : LightEffect::Paused;
  else if (s.now && !s.connected) effect_ = LightEffect::Connecting;
  else if (s.now) effect_ = s.playing ? LightEffect::Music : LightEffect::Paused;
  else if (s.timerRunning) effect_ = LightEffect::Timer;
  else if (s.timerPaused) effect_ = LightEffect::TimerPaused;
  else effect_ = LightEffect::Idle;

  LightFrame frame{};
  const float breath = (1.0f + std::sin(float(now % 3000) * 6.2831853f / 3000)) * .5f;
  for (unsigned i = 0; i < frame.size(); ++i) {
    float r = 0, g = 0, b = 0, a = 0;
    const float wave = .15f + .65f * (1 + std::sin(float(now % 4000) *
                                                    6.2831853f / 4000 - i * .9f)) * .5f;
    const bool edge = i == 0 || i + 1 == frame.size();
    const bool center = i == frame.size() / 2;
    const bool chase = (now / 180) % frame.size() == i;
    switch (effect_) {
    case LightEffect::Off: break;
    case LightEffect::Critical: r = 1; a = .25f + .55f * breath; break;
    case LightEffect::Warning: r = 1; g = .18f; a = edge ? .15f + .45f * breath : 0; break;
    case LightEffect::Update:
      r = .35f; b = 1;
      a = i * 100 < std::min(s.progress, 100u) * frame.size() ? .55f : .06f;
      if (chase) a = .7f;
      break;
    case LightEffect::Boot:
      g = .7f; b = 1; a = wave * (1 - float(now - bootAt_) / 1800); break;
    case LightEffect::SyncLost:
      r = 1; g = .3f; a = edge ? .15f + .5f * breath : 0; break;
    case LightEffect::Ap: b = 1; g = .2f; a = chase ? .55f : .035f; break;
    case LightEffect::TimerDone: r = .5f; g = 1; a = .15f + .6f * breath; break;
    case LightEffect::ArtMissing:
      r = 1; g = .35f; a = center ? .45f * (1 - float(now - feedbackAt_) / 1200) : 0; break;
    case LightEffect::ArtReady:
      g = 1; b = .3f; a = .5f * (1 - float(now - feedbackAt_) / 700); break;
    case LightEffect::ArtLoading: g = .4f; b = 1; a = chase ? .35f : .02f; break;
    case LightEffect::Sync: g = .9f; b = 1; a = wave; break;
    case LightEffect::Media: r = .4f; b = 1; a = wave; break;
    case LightEffect::Music: g = 1; b = .4f; a = wave; break;
    case LightEffect::Paused: g = .5f; b = 1; a = edge ? .12f : 0; break;
    case LightEffect::Connecting: b = 1; a = center ? .08f + .25f * breath : 0; break;
    case LightEffect::Timer: g = 1; b = .1f; a = .08f + .35f * breath; break;
    case LightEffect::TimerPaused: r = 1; g = .5f; a = center ? .12f : 0; break;
    case LightEffect::Idle: g = .7f; b = .4f; a = .07f; break;
    }
    frame[i] = {uint8_t(limit * std::clamp(r * a, 0.0f, 1.0f)),
                uint8_t(limit * std::clamp(g * a, 0.0f, 1.0f)),
                uint8_t(limit * std::clamp(b * a, 0.0f, 1.0f))};
  }
  return frame;
}
} // namespace nova

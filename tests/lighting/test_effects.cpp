#include "lighting/Effects.h"
#include <cassert>
#include <cstdint>
#include <iostream>
using namespace nova;
int main() {
  LightEffects e;
  LightState s;
  e.render(0, 100, s);
  assert(e.effect() == LightEffect::Boot);
  e.render(2000, 100, s);
  assert(e.effect() == LightEffect::Idle);
  // Core date/clock and D-day screens are lit even at the low brightness limits (6 at night, 7).
  {
    LightEffects core;
    LightState c;
    c.core = true;
    core.render(0, 6, c);
    for (const unsigned limit : {6u, 7u, 24u}) {
      for (const bool dday : {false, true}) {
        c.dday = dday;
        for (uint32_t now = 2000; now < 8000; now += 250) {
          const auto frame = core.render(now, uint8_t(limit), c);
          assert(core.effect() == (dday ? LightEffect::DDay : LightEffect::Clock));
          unsigned lit = 0;
          for (const auto &pixel : frame) lit += pixel.r || pixel.g || pixel.b;
          assert(lit == frame.size());  // Every LED shows something, never all dark.
        }
      }
    }
    c.dday = false;
    for (const auto &pixel : core.render(9000, 0, c)) assert(!pixel.r && !pixel.g && !pixel.b);  // LEDs off
  }
  s.now = s.connected = s.playing = s.track = true;
  s.trackGeneration = 1;
  // No artwork (offline, disabled, blocked, missing metadata) is not a failure.
  e.render(2100, 100, s); assert(e.effect() == LightEffect::Music);
  s.artLoading = true;
  e.render(2200, 100, s); assert(e.effect() == LightEffect::ArtLoading);
  e.render(10200, 100, s); assert(e.effect() == LightEffect::Music);
  s.artLoading = false; s.artFailed = true;
  e.render(11000, 100, s); assert(e.effect() == LightEffect::ArtMissing);
  e.render(12200, 100, s); assert(e.effect() == LightEffect::Music);
  // Retry/result or SD revision of the same song must not repeat amber feedback.
  s.artLoading = true;
  e.render(14000, 100, s); assert(e.effect() == LightEffect::Music);
  s.artLoading = false;
  e.render(15000, 100, s); assert(e.effect() == LightEffect::Music);
  s.artValid = true;
  e.render(16000, 100, s); assert(e.effect() == LightEffect::ArtReady);
  e.render(16700, 100, s); assert(e.effect() == LightEffect::Music);
  // A later failed refresh cannot mask a valid cover.
  e.render(17000, 100, s); assert(e.effect() == LightEffect::Music);
  s.trackGeneration++;
  s.artValid = false;
  e.render(18000, 100, s); assert(e.effect() == LightEffect::ArtMissing);
  // New-track state must not inherit the previous song's success or loading budget.
  s.trackGeneration++; s.artFailed = false; s.artLoading = true;
  e.render(19000, 100, s); assert(e.effect() == LightEffect::ArtLoading);
  s.media = true;
  e.render(19100, 100, s); assert(e.effect() == LightEffect::Media);
  s.synchronized = s.ap = true;
  e.render(19200, 100, s); assert(e.effect() == LightEffect::Sync);
  s.stale = true;
  e.render(19300, 100, s); assert(e.effect() == LightEffect::SyncLost);
  s.warning = true;
  e.render(19400, 100, s); assert(e.effect() == LightEffect::Warning);
  s.updating = true;
  e.render(19500, 100, s); assert(e.effect() == LightEffect::Update);
  s.critical = true;
  e.render(19600, 100, s); assert(e.effect() == LightEffect::Critical);
  s.sleeping = true;
  for (auto c : e.render(19700, 100, s)) assert(!(c.r || c.g || c.b));
  s.sleeping = false;
  for (auto c : e.render(19800, 0, s)) assert(!(c.r || c.g || c.b));
  // Boot must not recur at millis wrap. Notification deadlines also cross wrap.
  LightEffects wrap;
  s = {}; s.now = s.connected = s.track = true; s.trackGeneration = 9;
  wrap.render(UINT32_MAX - 4000, 100, s);
  wrap.render(UINT32_MAX - 2000, 100, s);
  s.artFailed = true;
  wrap.render(UINT32_MAX - 100, 100, s);
  assert(wrap.effect() == LightEffect::ArtMissing);
  wrap.render(1200, 100, s); assert(wrap.effect() == LightEffect::Paused);
  LightEffects cached;
  s = {}; s.now = s.connected = s.track = s.artValid = s.artFailed = true;
  cached.render(0, 100, s);
  cached.render(2000, 100, s);
  assert(cached.effect() == LightEffect::Paused);
  // Hidden feedback expires; leaving AP later must not replay an old failure.
  s = {}; s.now = s.connected = s.track = s.ap = s.artFailed = true;
  s.trackGeneration = 2;
  cached.render(3000, 100, s); assert(cached.effect() == LightEffect::Ap);
  s.ap = false;
  cached.render(5000, 100, s); assert(cached.effect() == LightEffect::Paused);
  // Bounds hold across brightness limits, time, and all state combinations.
  for (unsigned mask = 0; mask < 2048; ++mask) {
    s = {};
    s.critical = mask & 1; s.warning = mask & 2; s.updating = mask & 4;
    s.ap = mask & 8; s.synchronized = mask & 16; s.stale = mask & 32;
    s.media = mask & 64; s.playing = mask & 128; s.now = mask & 256;
    s.connected = mask & 512; s.timerFinished = mask & 1024;
    s.progress = 200;
    for (unsigned limit : {0u, 1u, 12u, 255u})
      for (auto c : e.render(25000 + mask * 40, limit, s))
        assert(c.r <= limit && c.g <= limit && c.b <= limit);
  }
  s = {}; s.ap = true;
  e.render(200000, 100, s); assert(e.effect() == LightEffect::Ap);
  s = {}; s.timerRunning = true;
  e.render(200100, 100, s); assert(e.effect() == LightEffect::Timer);
  s.timerRunning = false; s.timerPaused = true;
  e.render(200200, 100, s); assert(e.effect() == LightEffect::TimerPaused);
  s.timerFinished = true;
  e.render(200300, 100, s); assert(e.effect() == LightEffect::TimerDone);
  std::cout << "LED priorities, bounded artwork feedback/retry, valid-cover fallback, brightness and wrap passed\n";
}

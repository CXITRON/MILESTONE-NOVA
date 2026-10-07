#include "lighting/Effects.h"
#include "lighting/Lights.h"
#include <cmath>
#include <cassert>
#include <cstdint>
#include <iostream>
using namespace nova;
int main() {
  // Quantization error stays below one emitted count over a long frame sequence.
  for (unsigned limit : {1u, 6u, 7u, 24u, 96u, 255u}) {
    for (float fraction : {.02f, .15f, .4f, .9f, 1.0f}) {
      LightDither d;
      d.reset();
      const float target = limit * fraction;
      for (unsigned channel = 0; channel < board::rgbCount * 3; ++channel) {
        unsigned sum = 0;
        for (unsigned n = 0; n < 10000; ++n) {
          const auto out = d.channel(channel, target, limit);
          assert(out <= limit);
          sum += out;
        }
        assert(std::abs(double(sum) - double(target) * 10000) < 1.1);
        assert(d.channel(channel, 0, limit) == 0);
        assert(d.channel(channel, target, 0) == 0);
      }
    }
  }
  // Oscillating targets still conserve cumulative brightness; zero clears residuals.
  {
    LightDither d;
    d.reset();
    double sum = 0, wanted = 0;
    for (unsigned n = 0; n < 10000; ++n) {
      const float target = 3.4f + .7f * std::sin(n * .007f);
      sum += d.channel(0, target, 7);
      wanted += target;
    }
    assert(std::abs(sum - wanted) < 1.1);
    assert(d.channel(0, 100, 7) == 7);
    assert(d.channel(0, -1, 7) == 0);
  }
  // Real effect output matches fractional targets instead of the former minimum-one bias.
  {
    LightEffects paused;
    LightState state;
    state.now = state.connected = true;
    paused.render(0, 7, state);
    unsigned green = 0, blue = 0;
    constexpr unsigned count = 10000;
    for (unsigned n = 0; n < count; ++n) {
      const auto frame = paused.render(2000 + n * 8, 7, state);
      green += frame[0].g;
      blue += frame[0].b;
      assert(!frame[2].r && !frame[2].g && !frame[2].b);
    }
    // g = .5*.12, b = 1*.12, mild gamma x*(.8+.2*x).
    assert(std::abs(double(green) / count - .34104) < .0002);
    assert(std::abs(double(blue) / count - .69216) < .0002);
  }
  // Exercise the actual transport scheduler with a NeoPixel adapter.
  {
    Rgb rgb;
    LightState state;
    rgb.begin();
    const unsigned dark = mockLights::shows;
    for (unsigned n = 0; n < 100; ++n) rgb.tick(n, 0, state);
    assert(mockLights::shows == dark);
    rgb.tick(100, 7, state);
    const unsigned lit = mockLights::shows;
    for (unsigned n = 101; n < 1100; ++n) rgb.tick(n, 7, state);
    assert(mockLights::shows - lit == 124);  // one frame per 8 ms, no catch-up
    const auto before = mockLights::shows;
    mockLights::ready = false;
    rgb.tick(1200, 7, state);
    assert(mockLights::shows == before);
    mockLights::ready = true;
    rgb.tick(1300, 7, state);
    assert(mockLights::shows == before + 1);
    state.sleeping = true;
    rgb.tick(1301, 7, state); // OFF does not wait for the animation deadline.
    assert(mockLights::shows == before + 2);
    for (auto c : mockLights::pixels) assert(!c[0] && !c[1] && !c[2]);
    rgb.tick(1400, 7, state);
    assert(mockLights::shows == before + 2);
    state.sleeping = false;
    rgb.tick(UINT32_MAX - 3, 7, state);
    const auto wrapCount = mockLights::shows;
    rgb.tick(3, 7, state); assert(mockLights::shows == wrapCount);
    rgb.tick(4, 7, state); assert(mockLights::shows == wrapCount + 1);
  }
  // Slow main-loop cadence uses steady rounded output, then restores diffusion after recovery.
  {
    Rgb rgb;
    LightState state;
    state.now = state.connected = true;
    rgb.begin();
    rgb.tick(0, 7, state);
    for (uint32_t now = 2000; now < 2400; now += 40) {
      rgb.tick(now, 7, state);
      assert(mockLights::pixels[0][1] == 0 && mockLights::pixels[0][2] == 1);
    }
    unsigned changing = 0;
    for (uint32_t now = 2400; now < 3400; now += 8) {
      rgb.tick(now, 7, state);
      changing += mockLights::pixels[0][1] != 0;
    }
    assert(changing > 0);
    rgb.tick(3500, 7, state);
    assert(mockLights::pixels[0][1] == 0 && mockLights::pixels[0][2] == 1);
  }
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

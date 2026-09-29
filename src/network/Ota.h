#pragma once
#include "../settings/Values.h"
#include <atomic>
#include <cstdint>
namespace nova {
class Ota {
public:
  enum class State : uint8_t { Unconfigured, Closed, Listening, Receiving, Success, Failed };
  bool begin(const Secrets &secrets);
  void beginBootCheck();
  void checkBoot(uint32_t now);
  bool bootPending() const { return bootPending_; }
  bool open();
  void close();
  bool quiescent() const { return !requested_.load() && quiescent_.load(); }
  State state() const { return state_.load(); }
  unsigned progress() const { return progress_.load(); }
  const char *status() const;
  bool active() const {
    const auto s = state();
    return s == State::Listening || s == State::Receiving;
  }
  int error() const { return error_.load(); }

private:
  static void task(void *context);
  void run();
  Secrets secrets_{};
  std::atomic<State> state_{State::Unconfigured};
  std::atomic<bool> requested_{false}, quiescent_{true};
  std::atomic<unsigned> progress_{0};
  std::atomic<int> error_{0};
  bool configured_ = false;
  bool bootPending_ = false;
  uint32_t bootStarted_ = 0, bootLoops_ = 0;
};
} // namespace nova

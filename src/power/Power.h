#pragma once
#include <cstdint>
namespace nova {
class Power {
public:
  enum class State { Awake, Stopping, Release, Failed };
  bool request(uint32_t now, bool updating);
  void peripheralsStopped(uint32_t now);
  void tick(uint32_t now);
  State state() const { return state_; }
  bool pending() const { return state_ == State::Stopping || state_ == State::Release; }
  void fail() { state_ = State::Failed; }
  void cancel() { state_ = State::Awake; }

private:
  State state_ = State::Awake;
  uint32_t since_ = 0, released_ = 0;
};
} // namespace nova

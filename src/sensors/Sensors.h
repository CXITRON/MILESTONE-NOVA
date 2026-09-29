#pragma once
#include <cstdint>
#include <ctime>
namespace nova {
class Rtc {
public:
  bool read(time_t &utc);
  bool write(time_t utc);
  bool present() const { return present_; }

private:
  bool present_ = false;
};
class Environment {
public:
  void tick(uint32_t now);
  bool valid(uint32_t now) const { return sampled_ && now - lastGood_ < interval_ + 15000; }
  void interval(uint32_t ms) { interval_ = ms; }
  void rescan() {
    state_ = State::Probe;
    first_ = true;
    sampled_ = false;
  }
  uint32_t sampleTime() const { return lastGood_; }
  float temperature() const { return temperature_; }
  float humidity() const { return humidity_; }

private:
  enum class State { Probe, Calibrate, Trigger, Read };
  State state_ = State::Probe;
  uint32_t since_ = 0, lastGood_ = 0, interval_ = 5000;
  bool sampled_ = false, first_ = true;
  float temperature_ = 0, humidity_ = 0;
};
class Battery {
public:
  void begin();
  void tick(uint32_t now, float gain, float offset);
  bool valid() const { return valid_; }
  float volts() const { return voltage_; }
  float percent() const;
  bool low() const { return low_; }
  bool critical() const { return critical_; }

private:
  uint32_t last_ = 0, sum_ = 0;
  unsigned count_ = 0;
  bool valid_ = false, low_ = false, critical_ = false;
  float voltage_ = 0;
};
} // namespace nova

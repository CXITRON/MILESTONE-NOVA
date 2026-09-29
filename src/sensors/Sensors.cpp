#include "Sensors.h"
#include "../board/Board.h"
#include "../core/Logic.h"
#include <Arduino.h>
#include <Wire.h>
#include <algorithm>
namespace nova {
namespace {
bool readRegisters(uint8_t device, uint8_t reg, uint8_t *bytes, size_t n) {
  Wire.beginTransmission(device);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(device, n) != n)
    return false;
  for (size_t i = 0; i < n; ++i)
    bytes[i] = Wire.read();
  return true;
}
bool command(uint8_t a, const uint8_t *bytes, size_t n) {
  Wire.beginTransmission(a);
  Wire.write(bytes, n);
  return Wire.endTransmission() == 0;
}
int bcd(uint8_t b) { return (b & 15) > 9 || (b >> 4) > 9 ? -1 : (b >> 4) * 10 + (b & 15); }
uint8_t encode(int n) { return static_cast<uint8_t>((n / 10) * 16 + n % 10); }
} // namespace
bool Rtc::read(time_t &utc) {
  uint8_t r[7], status;
  present_ = readRegisters(0x68, 0, r, 7) && readRegisters(0x68, 0x0F, &status, 1);
  if (!present_ || (status & 0x80))
    return false;
  const int y = 2000 + bcd(r[6]) + ((r[5] & 0x80) ? 100 : 0), m = bcd(r[5] & 31),
            d = bcd(r[4] & 63);
  int h = bcd(r[2] & ((r[2] & 0x40) ? 31 : 63));
  const int min = bcd(r[1] & 127), sec = bcd(r[0] & 127);
  if (r[2] & 0x40) {
    if (h < 1 || h > 12)
      return false;
    h = h % 12 + ((r[2] & 32) ? 12 : 0);
  }
  if (!validDate(y, m, d) || h < 0 || h > 23 || min < 0 || min > 59 || sec < 0 || sec > 59 ||
      bcd(r[6]) < 0)
    return false;
  utc = static_cast<time_t>(civilDay(y, m, d)) * 86400 + h * 3600 + min * 60 + sec;
  return true;
}
bool Rtc::write(time_t utc) {
  tm t{};
  gmtime_r(&utc, &t);
  const int year = t.tm_year + 1900;
  if (!validDate(year, t.tm_mon + 1, t.tm_mday))
    return false;
  const uint8_t data[]{0,
                       encode(t.tm_sec),
                       encode(t.tm_min),
                       encode(t.tm_hour),
                       uint8_t(t.tm_wday + 1),
                       encode(t.tm_mday),
                       uint8_t(encode(t.tm_mon + 1) | (year >= 2100 ? 0x80 : 0)),
                       encode(year % 100)};
  uint8_t status;
  if (!command(0x68, data, sizeof(data)) || !readRegisters(0x68, 0x0F, &status, 1))
    return false;
  const uint8_t clear[]{0x0F, uint8_t(status & ~0x80)};
  present_ = command(0x68, clear, 2);
  return present_;
}
void Environment::tick(uint32_t now) {
  const auto fail = [&] {
    state_ = State::Probe;
    since_ = now;
  };
  if (state_ == State::Probe) {
    if (!first_ && now - since_ < 30000)
      return;
    first_ = false;
    Wire.beginTransmission(0x38);
    if (Wire.endTransmission() != 0) {
      fail();
      return;
    }
    const uint8_t init[]{0xE1, 0x08, 0x00}; // AHT10 (AHT20 uses a different init command).
    if (!command(0x38, init, 3)) {
      fail();
      return;
    }
    state_ = State::Calibrate;
    since_ = now;
    return;
  }
  if (state_ == State::Calibrate) {
    if (now - since_ < 350)
      return;
    state_ = State::Trigger;
    since_ = now - interval_;
  }
  if (state_ == State::Trigger) {
    if (now - since_ < interval_)
      return;
    const uint8_t trigger[]{0xAC, 0x33, 0x00};
    if (!command(0x38, trigger, 3)) {
      fail();
      return;
    }
    state_ = State::Read;
    since_ = now;
    return;
  }
  if (now - since_ < 85)
    return;
  if (Wire.requestFrom(uint8_t(0x38), size_t(6)) != 6) {
    fail();
    return;
  }
  uint8_t b[6];
  for (auto &v : b)
    v = Wire.read();
  if (b[0] & 0x80) {
    if (now - since_ > 200)
      fail();
    return;
  }
  if (!(b[0] & 8)) {
    fail();
    return;
  }
  const uint32_t rawH = (uint32_t(b[1]) << 12) | (uint32_t(b[2]) << 4) | (b[3] >> 4);
  const uint32_t rawT = (uint32_t(b[3] & 15) << 16) | (uint32_t(b[4]) << 8) | b[5];
  const float t = rawT * 200.0f / 1048576 - 50, h = rawH * 100.0f / 1048576;
  if (t < -40 || t > 85 || h > 100) {
    fail();
    return;
  }
  temperature_ = sampled_ ? temperature_ + (t - temperature_) * 0.25f : t;
  humidity_ = sampled_ ? humidity_ + (h - humidity_) * 0.25f : h;
  sampled_ = true;
  lastGood_ = now;
  since_ = now;
  state_ = State::Trigger;
}
void Battery::begin() {
  analogReadResolution(12);
  analogSetPinAttenuation(board::battery, ADC_11db);
}
void Battery::tick(uint32_t now, float gain, float offset) {
  if (now - last_ < 40)
    return;
  last_ = now;
  sum_ += analogReadMilliVolts(board::battery);
  if (++count_ < 32)
    return;
  const float v = (sum_ / 32.0f / 1000.0f) * 2 * gain + offset;
  sum_ = count_ = 0;
  if (v < 2.0f || v > 4.6f) {
    valid_ = false;
    low_ = critical_ = false;
    return;
  }
  voltage_ = valid_ ? voltage_ + (v - voltage_) * 0.2f : v;
  valid_ = true;
  low_ = voltage_ < (low_ ? 3.65f : 3.55f);
  critical_ = voltage_ < (critical_ ? 3.40f : 3.25f);
}
float Battery::percent() const { return valid_ ? batteryPercent(voltage_) : 0; }
} // namespace nova

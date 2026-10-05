#pragma once
#include "../settings/Values.h"
#include <cstddef>
#include <atomic>
#include <cstdint>
namespace nova {
struct WifiScanItem {
  char ssid[33]{};
  int16_t rssi = 0;
  uint8_t auth = 0;
  bool supported = false;
};
struct NetworkView {
  char status[64]{}, ip[24]{}, ssid[33]{}, test[96]{};
  bool connected = false, ap = false, scanning = false;
  unsigned scanCount = 0;
  WifiScanItem scan[24]{};
};
class Network {
public:
  void begin(const Secrets &, const Settings &);
  void configure(const Secrets &, const Settings &);
  void tick(uint32_t now, bool bleConnected);
  void stop();
  bool openAp(const Secrets &, const Settings &);
  void closeAp();
  bool ap() const { return ap_; }
  const char *apPassword() const { return apPassword_; }
  bool scan();
  bool test(const WifiProfile &);
  bool takeSaved(WifiProfile &);
  void saveResult(bool saved);
  void timeSync();
  bool connected() const;
  bool settled(uint32_t now) const;
  const char *status() const;
  void address(char *, size_t) const;
  void snapshot(NetworkView &) const;
  // Station drops since boot and the latest disconnect reason (WIFI_REASON_*), for diagnostics.
  uint32_t drops() const { return drops_; }
  unsigned lastReason() const { return reason_; }
  uint32_t failures() const { return failures_; }
  unsigned failureReason() const { return failureReason_; }
  int rssi() const;

private:
  void connect(const WifiProfile &);
  void disconnect(bool radioOff = false);
  WifiProfile profiles_[8]{}, candidate_{};
  unsigned count_ = 0, index_ = 0;
  char timezone_[48]{}, apPassword_[65]{}, testStatus_[96] = "대기";
  uint32_t started_ = 0, attempt_ = 0, retry_ = 30000, stable_ = 0, lastNtp_ = 0,
           ntpSeconds_ = 21600, retrySeconds_ = 300;
  bool eventRegistered_ = false;
  bool linkObserved_ = false;
  uint32_t connectedAt_ = 0;
  bool configured_ = false, attempting_ = false, ntp_ = false, off_ = false, ap_ = false,
       testing_ = false, saved_ = false, scanning_ = false, bootSync_ = true;
  NetworkView scans_{};
  std::atomic<uint32_t> drops_{0};
  std::atomic<unsigned> reason_{0}, failureReason_{0};
  std::atomic<uint32_t> failures_{0};
  std::atomic<bool> hadIp_{false}, localDisconnect_{false};
};
} // namespace nova

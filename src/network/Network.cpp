#include "Network.h"
#include "../logging/Log.h"
#include <WiFi.h>
#include <algorithm>
#include <cstring>
#include <ctime>
#include <esp_system.h>
namespace nova {
void Network::configure(const Secrets &s, const Settings &v) {
  count_ = std::min<unsigned>(s.networkCount, 8);
  memcpy(profiles_, s.networks, sizeof(profiles_));
  if (!count_ && s.ssid[0]) {
    strcpy(profiles_[0].ssid, s.ssid);
    strcpy(profiles_[0].password, s.password);
    if (validWifiProfile(profiles_[0]))
      count_ = 1;
  }
  configured_ = count_ > 0;
  if (index_ >= count_)
    index_ = 0;
  strcpy(timezone_, v.timezone);
  ntpSeconds_ = v.ntpSeconds;
  retrySeconds_ = v.retrySeconds;
  bootSync_ = v.bootSync;
  WiFi.setSleep(v.wifiSleep);
}
namespace {
// `now` is read at the start of a loop pass and may predate a connect() made later in that pass;
// a negative difference counts as no time elapsed instead of wrapping to a huge value.
constexpr uint32_t connectTimeoutMs = 30000;
constexpr uint32_t settleMs = 2000;
uint32_t elapsed(uint32_t now, uint32_t since) {
  return int32_t(now - since) > 0 ? now - since : 0;
}
} // namespace
void Network::disconnect(bool radioOff) {
  // Set before calling the driver: its event arrives on a different task.
  localDisconnect_ = true;
  hadIp_ = false;
  WiFi.disconnect(radioOff, false);
}
void Network::connect(const WifiProfile &p) {
  disconnect();
  linkObserved_ = false;
  WiFi.mode(ap_ ? WIFI_AP_STA : WIFI_STA);
  if (p.auth)
    WiFi.begin(p.ssid, WPA2_AUTH_PEAP, p.identity, p.username, p.password);
  else
    WiFi.begin(p.ssid, p.password[0] ? p.password : nullptr);
  attempting_ = true;
  attempt_ = millis();
  stable_ = 0;
  ntp_ = false;
}
void Network::begin(const Secrets &s, const Settings &v) {
  configure(s, v);
  started_ = attempt_ = millis();
  off_ = false;
  ntp_ = false;
  retry_ = 30000;
  testing_ = saved_ = false;
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  if (!eventRegistered_) {
    eventRegistered_ = true;
    WiFi.onEvent(
        [this](arduino_event_id_t event, arduino_event_info_t info) {
          if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
            localDisconnect_ = false;
          } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
            localDisconnect_ = false;
            hadIp_ = true;
          } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
            const unsigned reason = info.wifi_sta_disconnected.reason;
            const bool wasConnected = hadIp_.exchange(false);
            const bool local = localDisconnect_.exchange(false);
            // Only suppress a requested leave, never authentication/no-AP errors.
            if (local && (reason == WIFI_REASON_ASSOC_LEAVE || reason == WIFI_REASON_STA_LEAVING))
              return;
            if (wasConnected) {
              reason_ = reason;
              ++drops_;
            } else {
              failureReason_ = reason;
              ++failures_;
            }
          }
        });
  }
  WiFi.setHostname("milestone-nova");
  if (configured_) {
    index_ = 0;
    connect(profiles_[0]);
  } else {
    attempting_ = false;
    WiFi.mode(ap_ ? WIFI_AP_STA : WIFI_OFF);
  }
}
bool Network::openAp(const Secrets &s, const Settings &v) {
  if (ap_)
    return true;
  if (v.apMode == 1) {
    if (strlen(s.apPassword) < 8)
      return false;
    strcpy(apPassword_, s.apPassword);
  } else if (v.apMode == 2)
    apPassword_[0] = 0;
  else {
    constexpr char chars[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
    for (unsigned i = 0; i < 8; ++i)
      apPassword_[i] = chars[esp_random() % (sizeof(chars) - 1)];
    apPassword_[8] = 0;
  }
  off_ = false;
  WiFi.mode(WIFI_AP_STA);
  ap_ = WiFi.softAP("MILESTONE-NOVA-SETUP", apPassword_[0] ? apPassword_ : nullptr, 1, false, 4);
  return ap_;
}
void Network::closeAp() {
  if (!ap_)
    return;
  WiFi.softAPdisconnect(true);
  ap_ = false;
  WiFi.mode(configured_ || testing_ ? WIFI_STA : WIFI_OFF);
  memset(apPassword_, 0, sizeof(apPassword_));
}
bool Network::scan() {
  if (!ap_ || testing_ || scanning_)
    return false;
  WiFi.scanDelete();
  scanning_ = WiFi.scanNetworks(true, true) >= WIFI_SCAN_RUNNING;
  return scanning_;
}
bool Network::test(const WifiProfile &p) {
  if (!ap_ || !validWifiProfile(p) || testing_ || scanning_)
    return false;
  candidate_ = p;
  testing_ = true;
  saved_ = false;
  strcpy(testStatus_, "연결 시험 중");
  connect(p);
  return true;
}
bool Network::takeSaved(WifiProfile &p) {
  if (!saved_)
    return false;
  p = candidate_;
  saved_ = false;
  return true;
}
void Network::saveResult(bool saved) {
  strcpy(testStatus_, saved ? "연결 성공 · 저장됨" : "연결 성공 · 저장 실패, 다시 시험하세요");
}
int Network::rssi() const { return connected() ? WiFi.RSSI() : 0; }
bool Network::connected() const { return !off_ && WiFi.status() == WL_CONNECTED; }
bool Network::settled(uint32_t now) const {
  return !configured_ || off_ ||
         (connected() ? linkObserved_ && elapsed(now, connectedAt_) >= settleMs
                      : !attempting_ && elapsed(now, started_) >= connectTimeoutMs);
}
const char *Network::status() const {
  return connected()    ? "Connected"
         : off_         ? "Off"
         : testing_     ? "Testing"
         : !configured_ ? "Not configured"
         : attempting_  ? "Connecting"
                        : "Offline";
}
void Network::address(char *out, size_t n) const {
  snprintf(out, n, "%s", connected() ? WiFi.localIP().toString().c_str() : "--");
}
void Network::timeSync() {
  if (connected()) {
    configTzTime(timezone_, "pool.ntp.org", "time.cloudflare.com");
    lastNtp_ = millis();
    ntp_ = true;
  }
}
void Network::snapshot(NetworkView &v) const {
  v = scans_;
  v.connected = connected();
  v.ap = ap_;
  v.scanning = scanning_;
  snprintf(v.status, sizeof(v.status), "%s", status());
  address(v.ip, sizeof(v.ip));
  snprintf(v.ssid, sizeof(v.ssid), "%s", connected() ? WiFi.SSID().c_str() : "");
  strcpy(v.test, testStatus_);
}
void Network::tick(uint32_t now, bool ble) {
  if (scanning_) {
    const int n = WiFi.scanComplete();
    if (n >= 0) {
      scans_.scanCount = std::min(n, 24);
      for (unsigned i = 0; i < scans_.scanCount; ++i) {
        auto &e = scans_.scan[i];
        WiFi.SSID(i).toCharArray(e.ssid, sizeof(e.ssid));
        e.rssi = WiFi.RSSI(i);
        e.auth = WiFi.encryptionType(i);
        e.supported = e.auth == WIFI_AUTH_OPEN || e.auth == WIFI_AUTH_WPA_PSK ||
                      e.auth == WIFI_AUTH_WPA2_PSK || e.auth == WIFI_AUTH_WPA_WPA2_PSK ||
                      e.auth == WIFI_AUTH_WPA2_WPA3_PSK || e.auth == WIFI_AUTH_WPA2_ENTERPRISE;
      }
      scanning_ = false;
      WiFi.scanDelete();
    } else if (n == WIFI_SCAN_FAILED) {
      scanning_ = false;
      scans_.scanCount = 0;
    }
  }
  if (off_)
    return;
  if (connected()) {
    if (!linkObserved_) {
      linkObserved_ = true;
      connectedAt_ = now;
    }
    if (testing_) {
      if (!stable_)
        stable_ = now;
      else if (elapsed(now, stable_) >= 2000) {
        testing_ = false;
        attempting_ = false;
        saved_ = true;
        strcpy(testStatus_, "연결 성공, 저장 대기");
      }
    } else
      attempting_ = false;
    retry_ = 30000;
    if ((!ntp_ && bootSync_) ||
        (ntpSeconds_ && elapsed(now, lastNtp_) >= uint64_t(ntpSeconds_) * 1000))
      timeSync();
    return;
  }
  ntp_ = false;
  stable_ = 0;
  if (linkObserved_) {
    linkObserved_ = false;
    attempt_ = now; // Backoff starts at the loss, not an hours-old connect() timestamp.
  }
  if (attempting_ && elapsed(now, attempt_) >= connectTimeoutMs) {
    log("WIFI", "connection timeout after %lu ms (BLE=%u)",
        static_cast<unsigned long>(connectTimeoutMs), unsigned(ble));
    disconnect();
    attempting_ = false;
    attempt_ = now;
    if (testing_) {
      testing_ = false;
      strcpy(testStatus_, "연결 실패: 저장하지 않음");
    } else if (count_)
      index_ = (index_ + 1) % count_;
  }
  // A scan/connect can disturb a live BLE link, but never retrying while the iPhone is connected
  // (the whole NOW session) would leave a dropped Wi-Fi down forever; retry slowly instead.
  if (configured_ && !attempting_ && !testing_ && !scanning_ &&
      elapsed(now, attempt_) >= (ble ? std::max<uint32_t>(retry_, 60000) : retry_)) {
    connect(profiles_[index_]);
    retry_ = std::min<uint32_t>(retry_ * 2, retrySeconds_ * 1000);
  }
}
void Network::stop() {
  off_ = true;
  WiFi.softAPdisconnect(true);
  ap_ = false;
  disconnect(true);
  WiFi.mode(WIFI_OFF);
}
} // namespace nova

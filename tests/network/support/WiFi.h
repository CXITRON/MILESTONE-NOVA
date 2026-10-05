#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
inline uint32_t testMillis = 0;
inline uint32_t millis() { return testMillis; }
inline void configTzTime(const char *, const char *, const char *) {}
enum arduino_event_id_t { ARDUINO_EVENT_WIFI_STA_CONNECTED, ARDUINO_EVENT_WIFI_STA_GOT_IP,
                         ARDUINO_EVENT_WIFI_STA_DISCONNECTED };
struct arduino_event_info_t { struct { unsigned reason = 0; } wifi_sta_disconnected; };
constexpr int WL_CONNECTED = 3, WIFI_OFF = 0, WIFI_STA = 1, WIFI_AP_STA = 3, WPA2_AUTH_PEAP = 0,
              WIFI_SCAN_RUNNING = -1, WIFI_SCAN_FAILED = -2, WIFI_AUTH_OPEN = 0,
              WIFI_AUTH_WPA_PSK = 1, WIFI_AUTH_WPA2_PSK = 2, WIFI_AUTH_WPA_WPA2_PSK = 3,
              WIFI_AUTH_WPA2_WPA3_PSK = 4, WIFI_AUTH_WPA2_ENTERPRISE = 5,
              WIFI_REASON_ASSOC_LEAVE = 8, WIFI_REASON_STA_LEAVING = 36;
struct String : std::string {
  using std::string::string;
  void toCharArray(char *out, size_t n) const { snprintf(out, n, "%s", c_str()); }
};
struct Address { String toString() const { return "192.0.2.1"; } };
struct TestWiFi {
  int state = 0;
  unsigned starts = 0, disconnects = 0;
  std::function<void(arduino_event_id_t, arduino_event_info_t)> callback;
  void event(arduino_event_id_t event, unsigned reason = 0) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) state = 0;
    if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) state = WL_CONNECTED;
    if (callback) callback(event, {{reason}});
  }
  void onEvent(decltype(callback) cb) { callback = cb; }
  void disconnect(bool, bool) { state = 0; ++disconnects; } // Events may arrive later.
  void mode(int) {}
  void begin(const char *, const char *) { ++starts; }
  void begin(const char *, int, const char *, const char *, const char *) { ++starts; }
  void setSleep(bool) {}
  void persistent(bool) {}
  void setAutoReconnect(bool) {}
  void setHostname(const char *) {}
  bool softAP(const char *, const char *, int, bool, int) { return true; }
  void softAPdisconnect(bool) {}
  void scanDelete() {}
  int scanNetworks(bool, bool) { return WIFI_SCAN_RUNNING; }
  int scanComplete() { return WIFI_SCAN_RUNNING; }
  int encryptionType(unsigned) { return WIFI_AUTH_OPEN; }
  int status() const { return state; }
  int RSSI(unsigned = 0) const { return -55; }
  String SSID(unsigned = 0) const { return "test"; }
  Address localIP() const { return {}; }
};
inline TestWiFi WiFi;

#pragma once
inline constexpr int WL_CONNECTED = 3;
struct TestWiFi {
  int state = WL_CONNECTED;
  int status() const { return state; }
};
inline TestWiFi WiFi;

#include "network/Network.h"
#include <WiFi.h>
#include <cassert>
#include <cstring>
#include <iostream>
namespace nova { void log(const char *, const char *, ...) {} }
int main() {
  nova::Network network;
  nova::Secrets secrets;
  nova::Settings settings;
  strcpy(secrets.networks[0].ssid, "test");
  secrets.networkCount = 1;
  network.begin(secrets, settings);
  // A scheduled disconnect event is not a dropped established link.
  WiFi.event(ARDUINO_EVENT_WIFI_STA_DISCONNECTED, WIFI_REASON_ASSOC_LEAVE);
  assert(network.drops() == 0 && network.failures() == 0);
  testMillis = 15000;
  network.tick(testMillis, false);
  assert(!network.settled(testMillis) && WiFi.disconnects == 1);
  // Association is not IP readiness; BLE must wait through DHCP and stability interval.
  WiFi.event(ARDUINO_EVENT_WIFI_STA_CONNECTED);
  assert(!network.settled(testMillis));
  testMillis = 17000;
  WiFi.event(ARDUINO_EVENT_WIFI_STA_GOT_IP);
  network.tick(testMillis, false);
  assert(!network.settled(testMillis));
  testMillis = 19000;
  network.tick(testMillis, false);
  assert(network.settled(testMillis));
  testMillis = 1000000;
  WiFi.event(ARDUINO_EVENT_WIFI_STA_DISCONNECTED, 200);
  network.tick(testMillis, true);
  assert(network.drops() == 1 && network.lastReason() == 200 && WiFi.starts == 1);
  testMillis += 59999;
  network.tick(testMillis, true);
  assert(WiFi.starts == 1);
  ++testMillis;
  network.tick(testMillis, true);
  assert(WiFi.starts == 2);
  // A connect failure must not disappear behind a pending local-disconnect flag.
  WiFi.event(ARDUINO_EVENT_WIFI_STA_DISCONNECTED, 201);
  assert(network.failures() == 1 && network.failureReason() == 201 && network.drops() == 1);
  testMillis += 30000;
  network.tick(testMillis, true);
  WiFi.event(ARDUINO_EVENT_WIFI_STA_DISCONNECTED, WIFI_REASON_STA_LEAVING);
  assert(network.failures() == 1 && network.drops() == 1 && network.settled(testMillis));
  network.stop();
  WiFi.event(ARDUINO_EVENT_WIFI_STA_DISCONNECTED, WIFI_REASON_ASSOC_LEAVE);
  assert(network.drops() == 1 && network.failures() == 1);
  // Timer wrap: a connection started shortly before wrap still gets a full deadline.
  testMillis = UINT32_MAX - 10000;
  network.begin(secrets, settings);
  const auto disconnects = WiFi.disconnects;
  testMillis += 15000;
  network.tick(testMillis, false);
  assert(WiFi.disconnects == disconnects && !network.settled(testMillis));
  testMillis += 15000;
  network.tick(testMillis, false);
  assert(WiFi.disconnects == disconnects + 1 && network.settled(testMillis));
  std::cout << "Network lifecycle checks passed\n";
}

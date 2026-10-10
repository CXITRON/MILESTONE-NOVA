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
  // Completed boot waiting and link stability must survive the signed half-range and full wrap.
  testMillis = 0;
  network.begin(secrets, settings);
  testMillis = 30000;
  network.tick(testMillis, false);
  assert(network.settled(testMillis));
  for (auto now : {0x80000000U, 0xffffffffU, 0U})
    assert(network.settled(now));
  WiFi.event(ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.event(ARDUINO_EVENT_WIFI_STA_GOT_IP);
  testMillis = 31000;
  network.tick(testMillis, false);
  assert(!network.settled(testMillis));
  testMillis += 2000;
  network.tick(testMillis, false);
  assert(network.settled(testMillis));
  assert(network.settled(0x80010000U));
  // Beginning again resets the boot waiting latch.
  network.begin(secrets, settings);
  assert(!network.settled(testMillis));
  // Setup AP must remain on a stable channel; background STA reconnects are deferred.
  testMillis += 30000;
  network.begin(secrets, settings);
  const auto startsBeforeAp = WiFi.starts;
  assert(network.openAp(secrets, settings));
  testMillis += 600000;
  network.tick(testMillis, false);
  assert(WiFi.starts == startsBeforeAp);
  // Explicit AP Wi-Fi configuration tests remain available.
  assert(network.test(secrets.networks[0]));
  assert(WiFi.starts == startsBeforeAp + 1);
  testMillis += 30000;
  network.tick(testMillis, false);
  testMillis += 600000;
  network.tick(testMillis, false);
  assert(WiFi.starts == startsBeforeAp + 1);
  network.closeAp();
  network.tick(testMillis, false);
  assert(WiFi.starts == startsBeforeAp + 2);
  std::cout << "Network lifecycle checks passed\n";
}

#pragma once
#include <cstdint>
namespace nova {
// NOW layouts 0..nowLayouts-1 (4 = lyrics, 5 = cover + lyrics).
inline constexpr uint8_t nowLayouts = 6;
struct Settings {
  uint32_t schema = 2;
  uint32_t lcdHz = 20000000;
  uint32_t focusSeconds = 1500;
  uint16_t ddayYear = 2027;
  uint8_t ddayMonth = 1, ddayDay = 1;
  // heartbeatBrightness is unused (status LEDs removed) but kept for the stored NVS layout.
  uint8_t lcdBrightness = 160, rgbBrightness = 24, heartbeatBrightness = 24;
  bool lyricsView = true, displayInverted = true;
  float batteryGain = 1.0f, batteryOffset = 0.0f;
  char timezone[48] = "KST-9";
  char message[193] = "오늘도 한 걸음";
  uint8_t profile = 0, nowLayout = 4, coreStart = 0;
  bool hour24 = true, showSeconds = false, scroll = true, alignLeft = false;
  bool ddayText = false, afterComplete = false, cycle = false, burnin = true;
  uint8_t scrollSpeed = 24, cycleSeconds = 8;
  uint16_t coreMask = 511, screenOffMinutes = 0;
  uint8_t coreOrder[9] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
  char label[65] = "MILESTONE";
  uint16_t timeColor = 0xEF7D, dateColor = 0x5EB8, messageColor = 0x7C71, eventColor = 0xEF7D,
           accentColor = 0x5EB8, mutedColor = 0x7C71;
  uint8_t luminance = 100;
  int8_t contrast = 0;
  bool mediaMonochrome = false, mediaLoop = true, mediaAutoplay = false;
  uint16_t mediaSeconds = 15;
  uint8_t mediaSort = 0;
  bool environmentEnabled = true, fahrenheit = false, environmentLog = true;
  uint8_t environmentMask = 3;
  float temperatureOffset = 0, humidityOffset = 0;
  uint32_t sampleMs = 5000, logSeconds = 60;
  float temperatureLow = 10, temperatureHigh = 30, temperatureCritical = 40;
  float humidityLow = 20, humidityHigh = 70, humidityCritical = 85;
  bool ledsEnabled = true, wifiSleep = true, bootSync = true, artworkAuto = true;
  uint8_t nightBrightness = 6, apMode = 0;
  uint16_t nightStart = 1320, nightEnd = 420;
  uint32_t ntpSeconds = 21600, retrySeconds = 300;
  float thermalWarn = 70, thermalThrottle = 80, thermalStop = 90;
  uint32_t artworkCacheMb = 2048, artworkFreeMb = 1024;
};
struct WifiProfile {
  char ssid[33]{}, password[65]{}, identity[65]{}, username[65]{};
  uint8_t auth = 0; // 0 Personal/Open, 1 PEAP. Password stays private.
};
struct Secrets {
  char ssid[33]{};
  char password[65]{};
  // Unused since the app only updates from signed GitHub releases; kept so the stored Secrets
  // record (Wi-Fi profiles) keeps its size and stays readable.
  char otaPassword[65]{};
  char otaPublicKey[1024]{};
  char apPassword[65]{};
  WifiProfile networks[8]{};
  uint8_t networkCount = 0;
};
enum class SettingType : uint8_t { U8, U16, U32, I8, Float, Bool, Text };
struct SettingSpec {
  const char *name;
  SettingType type;
  unsigned offset;
  double minimum, maximum;
};
unsigned settingCount();
const SettingSpec &settingSpec(unsigned index);
bool settingValue(const Settings &settings, unsigned index, char *out, unsigned capacity);
bool validWifiProfile(const WifiProfile &profile);
bool rememberWifiProfile(Secrets &secrets, const WifiProfile &profile);
bool validSettings(const Settings &values);
bool applySetting(Settings &values, const char *name, const char *value);
} // namespace nova

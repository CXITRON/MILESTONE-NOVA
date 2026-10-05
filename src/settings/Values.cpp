#include "Values.h"
#include "../core/Logic.h"
#include "../core/Text.h"
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace nova {
bool rememberWifiProfile(Secrets &secrets, const WifiProfile &profile) {
  if (secrets.networkCount > 8 || !validWifiProfile(profile))
    return false;
  unsigned found = secrets.networkCount;
  for (unsigned i = 0; i < secrets.networkCount; ++i)
    if (!strcmp(secrets.networks[i].ssid, profile.ssid) &&
        secrets.networks[i].auth == profile.auth) {
      found = i;
      break;
    }
  if (found < secrets.networkCount)
    for (unsigned i = found; i + 1 < secrets.networkCount; ++i)
      secrets.networks[i] = secrets.networks[i + 1];
  else if (secrets.networkCount < 8)
    ++secrets.networkCount;
  for (unsigned i = secrets.networkCount - 1; i > 0; --i)
    secrets.networks[i] = secrets.networks[i - 1];
  secrets.networks[0] = profile;
  strcpy(secrets.ssid, profile.ssid);
  strcpy(secrets.password, profile.password);
  return true;
}
namespace {
#define S(name, member, type, lo, hi) {name, SettingType::type, offsetof(Settings, member), lo, hi}
const SettingSpec specs[]{S("lcd_hz", lcdHz, U32, 1000000, 40000000),
                          S("focus_seconds", focusSeconds, U32, 60, 14400),
                          S("lcd_brightness", lcdBrightness, U8, 8, 255),
                          S("rgb_brightness", rgbBrightness, U8, 0, 96),
                          S("lyrics_view", lyricsView, Bool, 0, 1),
                          S("display_inverted", displayInverted, Bool, 0, 1),
                          S("battery_gain", batteryGain, Float, .8, 1.2),
                          S("battery_offset", batteryOffset, Float, -.3, .3),
                          S("timezone", timezone, Text, 1, 48),
                          S("message", message, Text, 0, 193),
                          S("label", label, Text, 0, 65),
                          S("core_start", coreStart, U8, 0, 8),
                          S("profile", profile, U8, 0, 2),
                          S("now_layout", nowLayout, U8, 0, nowLayouts - 1),
                          S("hour24", hour24, Bool, 0, 1),
                          S("seconds", showSeconds, Bool, 0, 1),
                          S("scroll", scroll, Bool, 0, 1),
                          S("align_left", alignLeft, Bool, 0, 1),
                          S("dday_text", ddayText, Bool, 0, 1),
                          S("after_complete", afterComplete, Bool, 0, 1),
                          S("cycle", cycle, Bool, 0, 1),
                          S("burnin", burnin, Bool, 0, 1),
                          S("scroll_speed", scrollSpeed, U8, 5, 80),
                          S("cycle_seconds", cycleSeconds, U8, 3, 60),
                          S("core_mask", coreMask, U16, 1, 511),
                          S("screen_off_minutes", screenOffMinutes, U16, 0, 1440),
                          S("time_color", timeColor, U16, 0, 65535),
                          S("date_color", dateColor, U16, 0, 65535),
                          S("message_color", messageColor, U16, 0, 65535),
                          S("event_color", eventColor, U16, 0, 65535),
                          S("accent_color", accentColor, U16, 0, 65535),
                          S("muted_color", mutedColor, U16, 0, 65535),
                          S("luminance", luminance, U8, 50, 100),
                          S("contrast", contrast, I8, -20, 20),
                          S("media_monochrome", mediaMonochrome, Bool, 0, 1),
                          S("media_loop", mediaLoop, Bool, 0, 1),
                          S("media_autoplay", mediaAutoplay, Bool, 0, 1),
                          S("media_seconds", mediaSeconds, U16, 1, 3600),
                          S("media_sort", mediaSort, U8, 0, 2),
                          S("environment_enabled", environmentEnabled, Bool, 0, 1),
                          S("fahrenheit", fahrenheit, Bool, 0, 1),
                          S("environment_log", environmentLog, Bool, 0, 1),
                          S("environment_mask", environmentMask, U8, 0, 3),
                          S("temperature_offset", temperatureOffset, Float, -20, 20),
                          S("humidity_offset", humidityOffset, Float, -30, 30),
                          S("sample_ms", sampleMs, U32, 1000, 3600000),
                          S("log_seconds", logSeconds, U32, 5, 86400),
                          S("temperature_low", temperatureLow, Float, -40, 85),
                          S("temperature_high", temperatureHigh, Float, -40, 85),
                          S("temperature_critical", temperatureCritical, Float, -40, 85),
                          S("humidity_low", humidityLow, Float, 0, 100),
                          S("humidity_high", humidityHigh, Float, 0, 100),
                          S("humidity_critical", humidityCritical, Float, 0, 100),
                          S("leds_enabled", ledsEnabled, Bool, 0, 1),
                          S("night_brightness", nightBrightness, U8, 0, 96),
                          S("night_start", nightStart, U16, 0, 1439),
                          S("night_end", nightEnd, U16, 0, 1439),
                          S("wifi_sleep", wifiSleep, Bool, 0, 1),
                          S("boot_sync", bootSync, Bool, 0, 1),
                          S("ntp_seconds", ntpSeconds, U32, 0, 604800),
                          S("retry_seconds", retrySeconds, U32, 15, 86400),
                          S("ap_mode", apMode, U8, 0, 2),
                          S("artwork_auto", artworkAuto, Bool, 0, 1),
                          S("artwork_cache_mb", artworkCacheMb, U32, 1, 32768),
                          S("artwork_free_mb", artworkFreeMb, U32, 0, 8192),
                          S("thermal_warn", thermalWarn, Float, 45, 95),
                          S("thermal_throttle", thermalThrottle, Float, 50, 100),
                          S("thermal_stop", thermalStop, Float, 55, 105)};
#undef S
double numberAt(const Settings &s, const SettingSpec &d) {
  const auto *p = reinterpret_cast<const uint8_t *>(&s) + d.offset;
  switch (d.type) {
  case SettingType::U8:
    return *p;
  case SettingType::U16:
    return *reinterpret_cast<const uint16_t *>(p);
  case SettingType::U32:
    return *reinterpret_cast<const uint32_t *>(p);
  case SettingType::I8:
    return *reinterpret_cast<const int8_t *>(p);
  case SettingType::Float:
    return *reinterpret_cast<const float *>(p);
  case SettingType::Bool:
    return *reinterpret_cast<const bool *>(p);
  default:
    return 0;
  }
}
} // namespace
unsigned settingCount() { return sizeof(specs) / sizeof(specs[0]); }
const SettingSpec &settingSpec(unsigned i) { return specs[i % settingCount()]; }
bool settingValue(const Settings &s, unsigned i, char *out, unsigned cap) {
  if (i >= settingCount() || !cap)
    return false;
  const auto &d = specs[i];
  if (d.type == SettingType::Text)
    snprintf(out, cap, "%s", reinterpret_cast<const char *>(&s) + d.offset);
  else if (d.type == SettingType::Float)
    snprintf(out, cap, "%.3f", numberAt(s, d));
  else
    snprintf(out, cap, "%.0f", numberAt(s, d));
  return true;
}
bool validSettings(const Settings &s) {
  if (s.schema != 2 || !validDate(s.ddayYear, s.ddayMonth, s.ddayDay))
    return false;
  for (const auto &d : specs) {
    if (d.type == SettingType::Text) {
      const char *p = reinterpret_cast<const char *>(&s) + d.offset;
      if (!memchr(p, 0, unsigned(d.maximum)) || strlen(p) < d.minimum || !validUtf8(p))
        return false;
    } else {
      if (d.type == SettingType::Bool && *(reinterpret_cast<const uint8_t *>(&s) + d.offset) > 1)
        return false;
      const double n = numberAt(s, d);
      if (!std::isfinite(n) || n < d.minimum || n > d.maximum)
        return false;
    }
  }
  unsigned mask = 0;
  for (auto n : s.coreOrder) {
    if (n > 8 || (mask & (1U << n)))
      return false;
    mask |= 1U << n;
  }
  return s.temperatureLow < s.temperatureHigh && s.temperatureHigh <= s.temperatureCritical &&
         s.humidityLow < s.humidityHigh && s.humidityHigh <= s.humidityCritical &&
         s.thermalWarn < s.thermalThrottle && s.thermalThrottle < s.thermalStop;
}
bool applySetting(Settings &s, const char *key, const char *value) {
  if (!key || !value)
    return false;
  Settings c = s;
  if (!strcmp(key, "dday")) {
    int y, m, d;
    char tail;
    if (sscanf(value, "%d-%d-%d%c", &y, &m, &d, &tail) != 3 || !validDate(y, m, d))
      return false;
    c.ddayYear = y;
    c.ddayMonth = m;
    c.ddayDay = d;
  } else if (!strcmp(key, "core_order")) {
    if (strlen(value) != 17)
      return false;
    for (unsigned i = 0; i < 9; ++i) {
      if (value[i * 2] < '0' || value[i * 2] > '8' || (i < 8 && value[i * 2 + 1] != ','))
        return false;
      c.coreOrder[i] = value[i * 2] - '0';
    }
  } else {
    const SettingSpec *d = nullptr;
    for (const auto &spec : specs)
      if (!strcmp(key, spec.name)) {
        d = &spec;
        break;
      }
    if (!d)
      return false;
    auto *p = reinterpret_cast<uint8_t *>(&c) + d->offset;
    if (d->type == SettingType::Text) {
      if (strlen(value) >= d->maximum || strlen(value) < d->minimum || !validUtf8(value))
        return false;
      if (!strcmp(key, "timezone"))
        for (const char *v = value; *v; ++v)
          if (*v < 33 || *v > 126)
            return false;
      strcpy(reinterpret_cast<char *>(p), value);
    } else {
      char *end = nullptr;
      const double n = strtod(value, &end);
      if (end == value || *end || !std::isfinite(n) || n < d->minimum || n > d->maximum ||
          (d->type != SettingType::Float && floor(n) != n))
        return false;
      switch (d->type) {
      case SettingType::U8:
        *p = n;
        break;
      case SettingType::U16:
        *reinterpret_cast<uint16_t *>(p) = n;
        break;
      case SettingType::U32:
        *reinterpret_cast<uint32_t *>(p) = n;
        break;
      case SettingType::I8:
        *reinterpret_cast<int8_t *>(p) = n;
        break;
      case SettingType::Float:
        *reinterpret_cast<float *>(p) = n;
        break;
      case SettingType::Bool:
        *reinterpret_cast<bool *>(p) = n;
        break;
      default:
        return false;
      }
    }
  }
  if (!validSettings(c))
    return false;
  s = c;
  return true;
}
bool validWifiProfile(const WifiProfile &p) {
  if (!memchr(p.ssid, 0, sizeof(p.ssid)) || !p.ssid[0] || !validUtf8(p.ssid) || p.auth > 1 ||
      !memchr(p.password, 0, sizeof(p.password)) || !memchr(p.identity, 0, sizeof(p.identity)) ||
      !memchr(p.username, 0, sizeof(p.username)))
    return false;
  return p.auth ? p.username[0] && p.password[0]
                : !p.password[0] || (strlen(p.password) >= 8 && strlen(p.password) <= 63);
}
} // namespace nova

#include "LegacyImport.h"
#include "../core/Text.h"
#include <Preferences.h>
#include <algorithm>
#include <cstring>
namespace nova {
bool readLegacySettings(Settings &out, Secrets &secrets) {
  Preferences p;
  if (!p.begin("milestone", true))
    return false;
  const unsigned schema = p.getUShort("cfg_ver", 0);
  if (!schema || schema > 12) {
    p.end();
    return false;
  }
  Settings next = out;
  const auto text = [&](const char *old, const char *key) {
    if (p.isKey(old))
      applySetting(next, key, p.getString(old).c_str());
  };
  const auto integer = [&](const char *old, const char *key, uint32_t value) {
    if (p.isKey(old)) {
      char n[32];
      snprintf(n, sizeof(n), "%lu", static_cast<unsigned long>(value));
      applySetting(next, key, n);
    }
  };
  text("message", "message");
  text("title", "label");
  text("target", "dday");
  for (const auto &pair : {std::pair<const char *, const char *>{"hour24", "hour24"},
                           {"seconds", "seconds"},
                           {"msg_scroll", "scroll"},
                           {"msg_left", "align_left"},
                           {"dday_text", "dday_text"},
                           {"after_done", "after_complete"},
                           {"burnin", "burnin"},
                           {"media_mono", "media_monochrome"},
                           {"led_en", "leds_enabled"},
                           {"wifi_sleep", "wifi_sleep"},
                           {"boot_sync", "boot_sync"}})
    integer(pair.first, pair.second, p.getBool(pair.first));
  integer("scroll_spd", "scroll_speed", p.getUChar("scroll_spd", 24));
  integer("cycle_int", "cycle_seconds", p.getUChar("cycle_int", 8));
  integer("now_layout", "now_layout", p.getUChar("now_layout", 1));
  integer("screen_off", "screen_off_minutes", p.getUShort("screen_off", 0));
  integer("tone_lum", "luminance", p.getUChar("tone_lum", 92));
  if (p.isKey("tone_ctr")) {
    char n[16];
    snprintf(n, sizeof(n), "%d", p.getChar("tone_ctr", 8));
    applySetting(next, "contrast", n);
  }
  integer("led_night", "night_brightness", p.getUChar("led_night", 6));
  integer("night_start", "night_start", p.getUShort("night_start", 1320));
  integer("night_end", "night_end", p.getUShort("night_end", 420));
  integer("ntp_sec", "ntp_seconds", p.getUInt("ntp_sec", 21600));
  integer("retry_sec", "retry_seconds", p.getUInt("retry_sec", 300));
  const char *colors[]{"col_time", "col_date", "col_msg", "col_dday", "col_title", "col_info"};
  uint16_t *values[]{&next.timeColor,  &next.dateColor,   &next.messageColor,
                     &next.eventColor, &next.accentColor, &next.mutedColor};
  for (unsigned i = 0; i < 6; ++i)
    if (p.isKey(colors[i])) {
      const uint32_t rgb = p.getUInt(colors[i]);
      *values[i] = ((rgb >> 8) & 0xf800) | ((rgb >> 5) & 0x7e0) | ((rgb >> 3) & 31);
    }
  // Old CORE ordering: D-Day/clock, D-Day/message, message, clock, clock/message, dashboard,
  // system.
  constexpr uint8_t map[]{5, 1, 2, 0, 4, 3, 6};
  const uint8_t mode = p.getUChar("mode", 0);
  next.cycle = mode == 6;
  const unsigned oldView = mode == 6 ? p.getUChar("last_view", 0) : mode == 7 ? 6 : mode;
  next.coreStart = map[std::min(oldView, 6U)];
  const unsigned oldMask = p.getUChar("cycle_mask", 127) & 127;
  next.coreMask = 0;
  for (unsigned i = 0; i < 7; ++i)
    if (oldMask & (1U << i))
      next.coreMask |= 1U << map[i];
  if (!next.coreMask)
    next.coreMask = 127;
  const String order = p.getString("cycle_ord", "");
  unsigned used = 0, at = 0;
  for (char c : order) {
    if (c >= '0' && c <= '6' && !(used & (1U << (c - '0')))) {
      next.coreOrder[at++] = map[c - '0'];
      used |= 1U << (c - '0');
    }
  }
  for (unsigned i = 0; i < 7; ++i)
    if (!(used & (1U << i)))
      next.coreOrder[at++] = map[i];
  next.coreOrder[7] = 7;
  next.coreOrder[8] = 8;
  const String ap = p.getString("ap_pass", "");
  if (ap.length() >= 8 && ap.length() <= 63) {
    strcpy(secrets.apPassword, ap.c_str());
    next.apMode = p.getBool("ap_fixed", false) ? 1 : 0;
  }
  if (!secrets.networkCount) {
    const bool bank = p.getUChar("wifi_bank", 0) == 1;
    const unsigned count = std::min<unsigned>(8, p.getUChar(bank ? "wfb_count" : "wifi_count", 0));
    for (unsigned i = 0; i < count; ++i) {
      WifiProfile profile;
      char key[24];
      const char *prefix = bank ? "wfb_" : "wifi_";
      const auto load = [&](const char *field, char *target, size_t cap) {
        snprintf(key, sizeof(key), "%s%s%u", prefix, field, i);
        const String value = p.getString(key, "");
        if (value.length() < cap)
          strcpy(target, value.c_str());
      };
      load("ssid", profile.ssid, sizeof(profile.ssid));
      load("pass", profile.password, sizeof(profile.password));
      load("user", profile.username, sizeof(profile.username));
      load("id", profile.identity, sizeof(profile.identity));
      snprintf(key, sizeof(key), "%ssec%u", prefix, i);
      profile.auth = p.getUChar(key, 0);
      if (validWifiProfile(profile))
        secrets.networks[secrets.networkCount++] = profile;
    }
    if (!count) {
      WifiProfile profile;
      p.getString("wifi_ssid", profile.ssid, sizeof(profile.ssid));
      p.getString("wifi_pass", profile.password, sizeof(profile.password));
      if (validWifiProfile(profile))
        secrets.networks[secrets.networkCount++] = profile;
    }
    if (secrets.networkCount) {
      strcpy(secrets.ssid, secrets.networks[0].ssid);
      strcpy(secrets.password, secrets.networks[0].password);
    }
  }
  p.end();
  if (!validSettings(next))
    return false;
  out = next;
  return true;
}
} // namespace nova

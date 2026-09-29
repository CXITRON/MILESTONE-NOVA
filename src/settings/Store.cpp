#include "Store.h"
#include "../core/Text.h"
#include <Preferences.h>
#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
namespace nova {
namespace {
template <class T> struct Record {
  uint32_t magic = 0x4E564132, sequence = 0;
  T value{};
  uint32_t checksum = 0;
};
template <class T> bool read(Preferences &p, const char *key, Record<T> &r) {
  return p.getBytesLength(key) == sizeof(r) && p.getBytes(key, &r, sizeof(r)) == sizeof(r) &&
         r.magic == 0x4E564132 && crc32(&r, offsetof(Record<T>, checksum)) == r.checksum;
}
template <class T> bool loadRecord(Preferences &p, T &out, bool (*valid)(const T &)) {
  auto records = std::unique_ptr<Record<T>[]>(new (std::nothrow) Record<T>[2]);
  if (!records)
    return false;
  auto &a = records[0], &b = records[1];
  const bool x = read(p, "a", a) && valid(a.value), y = read(p, "b", b) && valid(b.value);
  if (!x && !y)
    return false;
  out = (y && (!x || int32_t(b.sequence - a.sequence) > 0)) ? b.value : a.value;
  return true;
}
template <class T> bool saveRecord(Preferences &p, const T &value) {
  auto records = std::unique_ptr<Record<T>[]>(new (std::nothrow) Record<T>[4]);
  if (!records)
    return false;
  auto &a = records[0], &b = records[1], &next = records[2], &check = records[3];
  const bool x = read(p, "a", a), y = read(p, "b", b);
  const bool latestB = y && (!x || int32_t(b.sequence - a.sequence) > 0);
  next.sequence = (latestB ? b.sequence : x ? a.sequence : 0) + 1;
  next.value = value;
  next.checksum = crc32(&next, offsetof(Record<T>, checksum));
  const char *key = latestB ? "a" : "b";
  return p.putBytes(key, &next, sizeof(next)) == sizeof(next) && read(p, key, check) &&
         memcmp(&next, &check, sizeof(next)) == 0;
}
bool validSecrets(const Secrets &s) {
  if (s.networkCount > 8 || !memchr(s.ssid, 0, sizeof(s.ssid)) ||
      !memchr(s.password, 0, sizeof(s.password)) ||
      !memchr(s.otaPassword, 0, sizeof(s.otaPassword)) ||
      !memchr(s.otaPublicKey, 0, sizeof(s.otaPublicKey)) ||
      !memchr(s.apPassword, 0, sizeof(s.apPassword)))
    return false;
  for (unsigned i = 0; i < s.networkCount; ++i)
    if (!validWifiProfile(s.networks[i]))
      return false;
  return !s.apPassword[0] || (strlen(s.apPassword) >= 8 && strlen(s.apPassword) <= 63);
}
// Read the previous NOVA record without modifying its layout or legacy namespaces.
struct SettingsV1 {
  uint32_t schema, lcdHz, focusSeconds;
  uint16_t year;
  uint8_t month, day, lcd, rgb, heartbeat;
  bool lyrics, inverted;
  float gain, offset;
  char timezone[48], message[193];
};
} // namespace
bool SettingsStore::load(Settings &s, Secrets &secrets) {
  s = Settings{};
  secrets = Secrets{};
  Preferences p;
  if (!p.begin("nova", false))
    return false;
  hasSettings_ = loadRecord(p, s, validSettings);
  if (!hasSettings_) {
    struct Old {
      SettingsV1 value;
      uint32_t checksum;
    } r{};
    if (p.getBytesLength("settings") == sizeof(r) &&
        p.getBytes("settings", &r, sizeof(r)) == sizeof(r) && r.value.schema == 1 &&
        crc32(&r.value, sizeof(r.value)) == r.checksum) {
      Settings c;
      const auto &v = r.value;
      c.lcdHz = v.lcdHz;
      c.focusSeconds = v.focusSeconds;
      c.ddayYear = v.year;
      c.ddayMonth = v.month;
      c.ddayDay = v.day;
      c.lcdBrightness = v.lcd;
      c.rgbBrightness = v.rgb;
      c.heartbeatBrightness = v.heartbeat;
      c.lyricsView = v.lyrics;
      c.displayInverted = v.inverted;
      c.batteryGain = v.gain;
      c.batteryOffset = v.offset;
      memcpy(c.timezone, v.timezone, sizeof(v.timezone));
      memcpy(c.message, v.message, sizeof(v.message));
      if (validSettings(c)) {
        s = c;
        hasSettings_ = true;
      }
    }
  }
  p.end();
  if (!p.begin("nova-secrets", false))
    return false;
  if (!loadRecord(p, secrets, validSecrets)) {
    p.getString("ssid", secrets.ssid, sizeof(secrets.ssid));
    p.getString("password", secrets.password, sizeof(secrets.password));
    p.getString("ota-pass", secrets.otaPassword, sizeof(secrets.otaPassword));
    p.getString("ota-key", secrets.otaPublicKey, sizeof(secrets.otaPublicKey));
    if (secrets.ssid[0]) {
      strcpy(secrets.networks[0].ssid, secrets.ssid);
      strcpy(secrets.networks[0].password, secrets.password);
      if (validWifiProfile(secrets.networks[0]))
        secrets.networkCount = 1;
    }
  }
  p.end();
  return true;
}
bool SettingsStore::save(const Settings &s) {
  if (!validSettings(s))
    return false;
  Preferences p;
  if (!p.begin("nova", false))
    return false;
  const bool ok = saveRecord(p, s);
  p.end();
  return ok;
}
bool SettingsStore::saveSecrets(const Secrets &s) {
  if (!validSecrets(s))
    return false;
  Preferences p;
  if (!p.begin("nova-secrets", false))
    return false;
  Secrets old;
  bool ok = loadRecord(p, old, validSecrets) && !memcmp(&old, &s, sizeof(s));
  if (!ok)
    ok = saveRecord(p, s);
  p.end();
  return ok;
}
bool SettingsStore::reset(bool credentials) {
  Preferences p;
  bool ok = p.begin("nova", false) && p.clear();
  p.end();
  if (credentials) {
    ok = (p.begin("nova-secrets", false) && p.clear()) && ok;
    p.end();
  }
  return ok;
}
} // namespace nova

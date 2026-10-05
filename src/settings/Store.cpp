#include "Store.h"
#include "../core/Text.h"
#include <Preferences.h>
#include <nvs.h>
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
template <class T> bool saveRecord(Preferences &p, const char *space, const T &value,
                                  esp_err_t &error) {
  error = ESP_OK;
  auto records = std::unique_ptr<Record<T>[]>(new (std::nothrow) Record<T>[4]);
  if (!records) {
    error = ESP_ERR_NO_MEM;
    return false;
  }
  auto &a = records[0], &b = records[1], &next = records[2], &check = records[3];
  const bool x = read(p, "a", a), y = read(p, "b", b);
  const bool latestB = y && (!x || int32_t(b.sequence - a.sequence) > 0);
  next.sequence = (latestB ? b.sequence : x ? a.sequence : 0) + 1;
  next.value = value;
  next.checksum = crc32(&next, offsetof(Record<T>, checksum));
  const char *key = latestB ? "a" : "b";
  // Preferences hides the NVS error; retain it to distinguish full flash from low RAM/I/O.
  nvs_handle_t handle;
  error = nvs_open(space, NVS_READWRITE, &handle);
  if (error != ESP_OK)
    return false;
  error = nvs_set_blob(handle, key, &next, sizeof(next));
  if (error == ESP_OK)
    error = nvs_commit(handle);
  nvs_close(handle);
  if (error == ESP_OK && (!read(p, key, check) || memcmp(&next, &check, sizeof(next))))
    error = ESP_ERR_INVALID_STATE;
  return error == ESP_OK;
}
template <class T> bool saveWithRecovery(Preferences &p, const char *space, const T &value,
                                        esp_err_t &error) {
  if (saveRecord(p, space, value, error))
    return true;
  if (error != ESP_ERR_NVS_NOT_ENOUGH_SPACE)
    return false;
  // History is expendable; settings, credentials and BLE bonds must never be cleared.
  Preferences diagnostics;
  if (!diagnostics.begin("nova-diag", false))
    return false;
  const bool cleared = diagnostics.clear();
  diagnostics.end();
  return cleared && saveRecord(p, space, value, error);
}
template <class T> bool validSecretsFields(const T &s) {
  if (s.networkCount > 8 || !memchr(s.ssid, 0, sizeof(s.ssid)) ||
      !memchr(s.password, 0, sizeof(s.password)) ||
      !memchr(s.apPassword, 0, sizeof(s.apPassword)))
    return false;
  for (unsigned i = 0; i < s.networkCount; ++i)
    if (!validWifiProfile(s.networks[i]))
      return false;
  return !s.apPassword[0] || (strlen(s.apPassword) >= 8 && strlen(s.apPassword) <= 63);
}
bool validSecrets(const Secrets &s) { return validSecretsFields(s); }
// Secrets layout up to v0.1.8. Its two unused OTA fields (1,089 bytes per copy) helped fill the
// 20 KiB NVS partition until saves failed, so the record was shrunk and is converted on load.
struct SecretsV1 {
  char ssid[33], password[65], otaPassword[65], otaPublicKey[1024], apPassword[65];
  WifiProfile networks[8];
  uint8_t networkCount;
};
static_assert(sizeof(SecretsV1) == 3085, "stored Secrets layout changed");
bool validSecretsV1(const SecretsV1 &s) { return validSecretsFields(s); }
void fromV1(const SecretsV1 &s, Secrets &out) {
  memcpy(out.ssid, s.ssid, sizeof(out.ssid));
  memcpy(out.password, s.password, sizeof(out.password));
  memcpy(out.apPassword, s.apPassword, sizeof(out.apPassword));
  memcpy(out.networks, s.networks, sizeof(out.networks));
  out.networkCount = s.networkCount;
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
  autoUpdate_ = p.getBool("update-auto", false);
  hasSettings_ = loadRecord(p, s, validSettings);
  if (hasSettings_ && p.isKey("settings"))
    p.remove("settings"); // Schema-1 blob, no longer needed once a current record exists.
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
  bool current = loadRecord(p, secrets, validSecrets);
  if (!current) {
    // The legacy blob is 3 KiB. Together with conversion temporaries it overflowed
    // Arduino's loopTask stack on first boot from v0.1.9. Keep it off the stack.
    auto old = std::unique_ptr<SecretsV1>(new (std::nothrow) SecretsV1{});
    if (!old) {
      p.end();
      return false;
    }
    if (loadRecord(p, *old, validSecretsV1)) {
      fromV1(*old, secrets);
      // Store the smaller record. The partition may be full, and diagnostics are expendable.
      esp_err_t error;
      const bool ok = saveWithRecovery(p, "nova-secrets", secrets, error);
      if (ok)
        for (const char *key : {"a", "b"})
          if (p.getBytesLength(key) == sizeof(Record<SecretsV1>))
            p.remove(key);
      current = ok;
    } else {
      p.getString("ssid", secrets.ssid, sizeof(secrets.ssid));
      p.getString("password", secrets.password, sizeof(secrets.password));
      if (secrets.ssid[0]) {
        strcpy(secrets.networks[0].ssid, secrets.ssid);
        strcpy(secrets.networks[0].password, secrets.password);
        if (validWifiProfile(secrets.networks[0]))
          secrets.networkCount = 1;
      }
    }
  }
  if (current)
    for (const char *key : {"ssid", "password", "ota-pass", "ota-key"})
      if (p.isKey(key))
        p.remove(key); // Pre-record strings, superseded by the Secrets record.
  p.end();
  return true;
}
bool SettingsStore::save(const Settings &s) {
  error_ = "";
  nvsFree_ = nvsTotal_ = 0;
  if (!validSettings(s)) {
    error_ = "invalid";
    return false;
  }
  Preferences p;
  if (!p.begin("nova", false)) {
    error_ = "open";
    return false;
  }
  esp_err_t error;
  const bool ok = saveWithRecovery(p, "nova", s, error);
  p.end();
  if (!ok) {
    error_ = esp_err_to_name(error);
    nvs_stats_t stats{};
    if (nvs_get_stats(nullptr, &stats) == ESP_OK) {
      nvsFree_ = stats.free_entries;
      nvsTotal_ = stats.total_entries;
    }
  }
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
  if (!ok) {
    esp_err_t error;
    ok = saveWithRecovery(p, "nova-secrets", s, error);
  }
  p.end();
  return ok;
}
bool SettingsStore::reset(bool credentials) {
  Preferences p;
  bool ok = p.begin("nova", false) && p.clear();
  p.end();
  if (ok)
    autoUpdate_ = false;
  if (credentials) {
    ok = (p.begin("nova-secrets", false) && p.clear()) && ok;
    p.end();
  }
  return ok;
}
bool SettingsStore::saveAutoUpdate(bool enabled) {
  Preferences p;
  if (!p.begin("nova", false))
    return false;
  const bool ok = p.putBool("update-auto", enabled) == 1 &&
                  p.getBool("update-auto", !enabled) == enabled;
  p.end();
  if (ok)
    autoUpdate_ = enabled;
  return ok;
}
} // namespace nova

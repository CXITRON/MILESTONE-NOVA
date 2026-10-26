#pragma once
#include "Values.h"
namespace nova {
class SettingsStore {
public:
  bool hasSettings() const { return hasSettings_; }
  bool load(Settings &settings, Secrets &secrets);
  bool save(const Settings &settings);
  bool saveSecrets(const Secrets &secrets);
  bool reset(bool credentials);
  bool autoUpdate() const { return autoUpdate_; }
  bool saveAutoUpdate(bool enabled);
  // Screen language (0 Korean, 1 Siron). Its own NVS key like "update-auto", so the Settings
  // record layout and existing saved settings stay valid.
  uint8_t uiLanguage() const { return uiLanguage_; }
  bool saveUiLanguage(uint8_t language);
  // Validation/open failure or the exact ESP-IDF write/commit error, plus NVS entries then.
  const char *lastError() const { return error_; }
  unsigned nvsFree() const { return nvsFree_; }
  unsigned nvsTotal() const { return nvsTotal_; }
  // Free/total NVS entries now, for the diagnostics header. Walks the partition, so call it rarely.
  void refreshNvsUsage();
  unsigned nvsUsageFree() const { return usageFree_; }
  unsigned nvsUsageTotal() const { return usageTotal_; }

private:
  bool hasSettings_ = false;
  bool autoUpdate_ = false;
  uint8_t uiLanguage_ = 0;
  const char *error_ = "";
  unsigned nvsFree_ = 0, nvsTotal_ = 0, usageFree_ = 0, usageTotal_ = 0;
};
} // namespace nova

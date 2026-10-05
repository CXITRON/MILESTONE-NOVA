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
  const char *error_ = "";
  unsigned nvsFree_ = 0, nvsTotal_ = 0, usageFree_ = 0, usageTotal_ = 0;
};
} // namespace nova

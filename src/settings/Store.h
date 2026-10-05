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

private:
  bool hasSettings_ = false;
  bool autoUpdate_ = false;
};
} // namespace nova

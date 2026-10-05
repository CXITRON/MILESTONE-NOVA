#pragma once
#include "../display/Display.h"
#include "../input/Buttons.h"
#include "../input/Console.h"
#include "../lighting/Lights.h"
#include "../logging/Diagnostics.h"
#include "../media/BleMedia.h"
#include "../media/Playback.h"
#include "../media/TrackAssets.h"
#include "../network/Network.h"
#include "../portal/Portal.h"
#include "../power/Power.h"
#include "../sensors/Sensors.h"
#include "../settings/Store.h"
#include "../storage/Storage.h"
#include "../ui/Ui.h"
#include "../update/AutoUpdate.h"
#include "../update/BootConfirm.h"
namespace nova {
class App {
public:
  void begin();
  void tick();

private:
  void input(const InputEvent &event, uint32_t now);
  void console(const char *command, uint32_t now);
  void changeSetting(int direction, uint32_t now);
  void assets(uint32_t now);
  void internetUpdate(uint32_t now);
  void invalidateTrackAssets(uint32_t now, bool trackChanged = false, bool retryOnline = true);
  void render(uint32_t now);
  void shutdown(uint32_t now);
  void selectMode(Profile profile, uint32_t now);
  void selectMedia(int direction, uint32_t now);
  void portalCommands(uint32_t now);
  void publish(uint32_t now, bool force = false);
  void applySettings(uint32_t now);
  void closeAp(uint32_t now);
  void notice(const char *message, uint32_t now);
  Settings settings_;
  Secrets secrets_;
  SettingsStore store_;
  Display display_;
  Buttons buttons_;
  Console console_;
  Storage storage_;
  Rtc rtc_;
  Environment environment_;
  Battery battery_;
  Rgb rgb_;
  BleMedia ble_;
  MediaSession session_;
  Network network_;
  BootConfirm bootConfirm_;
  Power power_;
  FocusTimer timer_;
  Ui ui_;
  Navigation navigation_;
  Playback playback_;
  Artwork artwork_;
  OnlineLyrics onlineLyrics_;
  TrackAssets trackAssets_;
  Firmware firmware_;
  AutoUpdate autoUpdate_;
  Firmware::State lastUpdateState_ = Firmware::State::Idle;
  Portal portal_;
  Diagnostics diagnostics_;
  PortalSnapshot *snapshot_ = nullptr;
  MediaCatalog *catalog_ = nullptr;
  Lyrics *lyrics_ = nullptr;
  uint16_t *cover_ = nullptr;
  uint16_t *mediaPixels_ = nullptr;
  Screen screen_ = Screen::Clock;
  uint32_t lastInput_ = 0, lastCycle_ = 0, lastPublish_ = 0, lastEnvironmentLog_ = 0,
           lastThermal_ = 0, stillSince_ = 0;
  unsigned systemPage_ = 0, recoveryItem_ = 0;
  int lastControlResult_ = 0, thermalState_ = 0;
  float chipTemperature_ = 0;
  bool serviceReady_ = false, screenOff_ = false, rebootRequested_ = false, apClosing_ = false;
  uint32_t boot_ = 0, rendered_ = 0, lastLog_ = 0, lastRtc_ = 0, lastLoop_ = 0, loopMax_ = 0;
  uint32_t savedAt_ = 0, lastGeneration_ = 0, mediaGeneration_ = 1, mediaRequested_ = 0;
  uint32_t lastAssetRevision_ = 0;
  uint32_t noticed_ = 0, shutdownAt_ = 0;
  unsigned setting_ = 0, mediaItem_ = 0;
  bool bleStarted_ = false, settingsDirty_ = false, settingsEditing_ = false;
  bool mediaValid_ = false, assetsNeeded_ = false, assetPending_ = false,
       mediaFailed_ = false, assetError_ = false;
  bool shutdownStarted_ = false, storageStopping_ = false, peripheralsOff_ = false;
  char notice_[128]{}, address_[24]{};
};
} // namespace nova

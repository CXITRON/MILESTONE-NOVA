#pragma once
#include "../artwork/Artwork.h"
#include "../lyrics/OnlineLyrics.h"
#include "../network/Network.h"
#include "../ui/Navigation.h"
#include "../update/Firmware.h"
#include <DNSServer.h>
#include <WebServer.h>
#include <atomic>
namespace nova {
enum class CommandKind : uint8_t {
  Settings,
  Mode,
  WifiScan,
  WifiTest,
  WifiDelete,
  WifiUse,
  ApPassword,
  ApClose,
  Time,
  Ntp,
  LegacyImport,
  Defaults,
  FactoryReset,
  SensorScan,
  SyncStart,
  SyncTick,
  SyncStop,
  ArtRefresh,
  UpdateCheck,
  UpdateDownload,
  UpdatePrepare,
  UpdateAuto,
  LogsClear,
  Restart
};
struct PortalCommand {
  CommandKind kind = CommandKind::Settings;
  Settings settings{};
  WifiProfile wifi{};
  uint32_t id = 0, sequence = 0, position = 0, duration = 0;
  int64_t epoch = 0;
  bool playing = false, ok = false;
  char text[193]{}, error[128]{};
};
struct PortalSnapshot {
  Settings settings{};
  NetworkView network{};
  Track track{};
  char wifiSaved[8][33]{}, ble[64]{}, message[128]{}, diagnostics[4096]{};
  unsigned wifiCount = 0, mode = 0, mediaIndex = 0;
  uint32_t uptime = 0, heap = 0, psram = 0, position = 0, syncSession = 0;
  float temperature = 0, humidity = 0, volts = 0;
  bool sensor = false, sd = false, playing = false, syncStale = false, autoUpdate = false;
};
// HTTP/DNS and outbound I/O have one worker; hardware/NVS/UI remain on App's loop.
class Portal {
public:
  bool begin(Storage &storage, Artwork &artwork, OnlineLyrics &lyrics, Firmware &firmware);
  void open(bool enabled);
  void suspend(bool enabled);
  bool suspended() const { return !ready_ || (suspendRequested_ && suspended_); }
  void cleanupSync() { cleanupSync_ = true; }
  bool quiescent() const { return !requested_ && quiescent_; }
  void publish(const PortalSnapshot &snapshot);
  PortalCommand *receive();
  void reply(PortalCommand &command);
  bool logEnvironment(const char *path, const char *line);

private:
  static void task(void *self);
  static void commitTask(void *self);
  void run();
  void routes();
  bool authorized();
  bool dispatch(PortalCommand &command);
  void status();
  void commitStatus();
  void settings();
  void action();
  void media();
  void files();
  void file();
  void transfer();
  void chunk();
  void upload();
  void readJson();
  String body();
  void artwork();
  void respond(bool ok, const char *message = "");
  void json(void *value, int status = 200);
  void snapshot();
  struct LogLine {
    char path[80]{}, line[256]{};
  };
  Storage *storage_ = nullptr;
  Artwork *artwork_ = nullptr;
  OnlineLyrics *lyrics_ = nullptr;
  Firmware *firmware_ = nullptr;
  WebServer server_{80};
  DNSServer dns_;
  QueueHandle_t commands_ = nullptr, logs_ = nullptr;
  SemaphoreHandle_t done_ = nullptr, mutex_ = nullptr;
  PortalSnapshot *published_ = nullptr, *view_ = nullptr;
  MediaCatalog *catalog_ = nullptr;
  PortalCommand command_{};
  uint8_t *bytes_ = nullptr;
  size_t received_ = 0;
  bool uploadOk_ = false, jsonReady_ = false, ready_ = false;
  char token_[33]{};
  std::atomic<bool> requested_{false}, quiescent_{true}, cleanupSync_{false};
  std::atomic<bool> suspendRequested_{false}, suspended_{false};
  // Validating a large upload takes minutes; it runs outside the HTTP task so progress can be polled.
  enum class Commit : uint8_t { Idle, Running, Done, Failed };
  std::atomic<Commit> commit_{Commit::Idle};
  uint32_t commitId_ = 0;
  char commitPath_[128]{}, commitMessage_[96]{};
};
} // namespace nova

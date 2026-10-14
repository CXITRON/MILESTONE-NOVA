#pragma once
#include "Release.h"
#include "../storage/Storage.h"
#include <atomic>
class UpdateClass;
class UpdaterRSAVerifier;
namespace nova {
class Firmware {
public:
  // Direct is an open SD-free upload session; DirectInstall finishes it on the service worker.
  enum class Work : uint8_t {
    None, Check, Download, Prepare, Install, AutoInstall, Rollback, Direct, DirectInstall
  };
  using State = ReleaseState;
  // Releases are verified with the key built into the firmware. Only tests pass an override.
  void begin(Storage &storage, const char *publicKeyOverride = nullptr);
  // App sets this only after the service worker has been successfully created.
  void serviceReady(bool ready) { serviceReady_ = ready; }
  // App sets this after begin and before serviceReady; recovery skips SD and media services.
  void recoveryMode(bool enabled) { recovery_ = enabled; }
  bool recoveryMode() const { return recovery_; }
  // App's power/thermal gate for direct uploads. Off until App's first safe tick; turning it off
  // during a transfer aborts the session on the next worker step.
  void directPermitted(bool permitted) { directPermitted_ = permitted; }
  bool request(Work work);
  void process(); // Service worker only.
  bool busy() const { return work_ != Work::None || state_ == State::Working; }
  bool installing() const {
    const auto work = work_.load();
    return work == Work::Install || work == Work::AutoInstall || work == Work::Rollback ||
           work == Work::DirectInstall;
  }
  // SD-free upload of a signed image straight into the inactive OTA slot. Calls come from the
  // service worker (Portal's HTTP handlers), the same task that runs process(). The boot
  // partition changes only after the RSA signature verifies in directFinish's worker step.
  bool directBegin(uint32_t id, uint32_t total);
  bool directChunk(uint32_t id, uint32_t offset, const uint8_t *data, size_t length, uint32_t crc);
  bool directFinish(uint32_t id);
  bool directAbort(uint32_t id);
  uint32_t directId() const { return directId_; }
  uint32_t directReceived() const { return directAt_; }
  uint32_t directTotal() const { return directTotal_; }
  State state() const { return state_; }
  unsigned progress() const { return progress_; }
  void status(char *out, size_t capacity);
  // Version of the newest release found by the last check, empty when none is newer.
  void latest(char *out, size_t capacity);

private:
  bool check();
  bool download();
  bool prepare(bool release = false);
  bool install();
  bool autoInstall();
  bool rollback();
  bool directInstall();
  bool directFail(const char *reason);
  void message(const char *text);
  Storage *storage_ = nullptr;
  SemaphoreHandle_t mutex_ = nullptr;
  char key_[1024]{}, status_[128] = "No candidate selected", url_[256]{}, version_[48]{},
                     hash_[65]{};
  uint8_t preparedHash_[32]{};
  uint8_t *bytes_ = nullptr;
  uint32_t size_ = 0, preparedSize_ = 0;
  bool releaseCandidate_ = false;
  UpdateClass *updater_ = nullptr;
  UpdaterRSAVerifier *verifier_ = nullptr;
  uint32_t directLastOffset_ = 0, directLastLength_ = 0, directLastCrc_ = 0, directSeen_ = 0;
  std::atomic<uint32_t> directId_{0}, directAt_{0}, directTotal_{0};
  std::atomic<bool> recovery_{false}, directPermitted_{false};
  std::atomic<bool> serviceReady_{false};
  std::atomic<Work> work_{Work::None};
  std::atomic<State> state_{State::Idle};
  std::atomic<unsigned> progress_{0};
};
} // namespace nova

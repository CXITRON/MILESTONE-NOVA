#pragma once
#include "Release.h"
#include "../storage/Storage.h"
#include <atomic>
namespace nova {
class Firmware {
public:
  enum class Work : uint8_t { None, Check, Download, Prepare, Install, AutoInstall, Rollback };
  using State = ReleaseState;
  // Releases are verified with the key built into the firmware. Only tests pass an override.
  void begin(Storage &storage, const char *publicKeyOverride = nullptr);
  bool request(Work work);
  void process(); // Service worker only.
  bool busy() const { return work_ != Work::None || state_ == State::Working; }
  bool installing() const {
    const auto work = work_.load();
    return work == Work::Install || work == Work::AutoInstall || work == Work::Rollback;
  }
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
  void message(const char *text);
  Storage *storage_ = nullptr;
  SemaphoreHandle_t mutex_ = nullptr;
  char key_[1024]{}, status_[128] = "No candidate selected", url_[256]{}, version_[48]{},
                     hash_[65]{};
  uint8_t preparedHash_[32]{};
  uint8_t *bytes_ = nullptr;
  uint32_t size_ = 0, preparedSize_ = 0;
  bool releaseCandidate_ = false;
  std::atomic<Work> work_{Work::None};
  std::atomic<State> state_{State::Idle};
  std::atomic<unsigned> progress_{0};
};
} // namespace nova

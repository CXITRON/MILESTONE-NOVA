#pragma once
#include "../storage/Storage.h"
#include <atomic>
namespace nova {
class Firmware {
public:
  enum class Work : uint8_t { None, Check, Download, Prepare, Install, Rollback };
  enum class State : uint8_t { Idle, Working, Available, Ready, Success, Failed };
  void begin(Storage &storage, const char *publicKey);
  bool request(Work work);
  void process(); // Service worker only. Install is requested by a physical long press.
  bool busy() const { return work_ != Work::None || state_ == State::Working; }
  State state() const { return state_; }
  unsigned progress() const { return progress_; }
  void status(char *out, size_t capacity);

private:
  bool check();
  bool download();
  bool prepare();
  bool install();
  bool rollback();
  void message(const char *text);
  Storage *storage_ = nullptr;
  SemaphoreHandle_t mutex_ = nullptr;
  char key_[1024]{}, status_[128] = "No candidate selected", url_[256]{}, version_[48]{},
                     hash_[65]{};
  uint8_t preparedHash_[32]{};
  uint8_t *bytes_ = nullptr;
  uint32_t size_ = 0, preparedSize_ = 0;
  std::atomic<Work> work_{Work::None};
  std::atomic<State> state_{State::Idle};
  std::atomic<unsigned> progress_{0};
};
} // namespace nova

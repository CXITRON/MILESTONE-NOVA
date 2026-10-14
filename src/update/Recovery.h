#pragma once
#include <cstddef>
#include <cstdint>
namespace nova {
// MCU RTC RAM, not the external RTC module or NVS. Power loss clears this safety hint.
class Recovery {
public:
  void begin(uint32_t now, bool manual, bool crashReset, bool coldReset);
  void tick(uint32_t now);
  bool active() const { return active_; }
  unsigned failures() const { return failures_; }
  void resumeNormal(); // Explicit user retry or successful update; no SD/NVS edits.
  static bool blocked(const char *path);
  static void suspect(char *out, size_t cap);
  class FileScope {
  public:
    explicit FileScope(const char *path);
    ~FileScope();
    FileScope(const FileScope &) = delete;
    FileScope &operator=(const FileScope &) = delete;
  private:
    char previous_[128]{};
  };
private:
  bool active_ = false, completed_ = false;
  unsigned failures_ = 0;
  uint32_t started_ = 0, loops_ = 0;
};
} // namespace nova

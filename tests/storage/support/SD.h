#pragma once
#include "FS.h"
#include "SPI.h"
#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"
class TestSD {
public:
  std::string root;
  bool begin(int, SPIClass &, uint32_t) { return true; }
  void end() {}
  bool exists(const char *path) const { return std::filesystem::exists(root + path); }
  bool mkdir(const char *path) { return std::filesystem::create_directories(root + path); }
  bool remove(const char *path) { return std::filesystem::remove(root + path); }
  bool rename(const char *from, const char *to) {
    if (failRenameSource == from) {
      failRenameSource.clear();
      return false;
    }
    std::error_code e;
    std::filesystem::rename(root + from, root + to, e);
    return !e;
  }
  File open(const char *path, const char *mode = FILE_READ) {
    return File(root + path, path, mode);
  }
  uint64_t totalBytes() const { return 16ULL * 1024 * 1024 * 1024; }
  uint64_t usedBytes() const {
    uint64_t n = 0;
    for (auto &p : std::filesystem::recursive_directory_iterator(root))
      if (p.is_regular_file())
        n += p.file_size();
    return n;
  }
};
extern TestSD SD;

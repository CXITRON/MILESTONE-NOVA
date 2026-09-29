#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
inline int failWriteAfter = -1;
inline std::string failRenameSource;
class File {
  struct Handle {
    FILE *file = nullptr;
    std::string logical, absolute;
    std::vector<std::filesystem::path> entries;
    size_t next = 0;
    bool directory = false;
    ~Handle() {
      if (file)
        fclose(file);
    }
  };
  std::shared_ptr<Handle> h_;

public:
  File() = default;
  File(const std::string &absolute, const std::string &logical, const char *mode = "r") {
    h_ = std::make_shared<Handle>();
    h_->absolute = absolute;
    h_->logical = logical;
    h_->directory = std::filesystem::is_directory(absolute);
    if (h_->directory) {
      for (const auto &p : std::filesystem::directory_iterator(absolute))
        h_->entries.push_back(p.path());
      std::sort(h_->entries.begin(), h_->entries.end());
    } else
      h_->file = fopen(absolute.c_str(), mode);
  }
  explicit operator bool() const { return h_ && (h_->file || h_->directory); }
  bool isDirectory() const { return h_ && h_->directory; }
  uint32_t size() const { return h_ && h_->file ? std::filesystem::file_size(h_->absolute) : 0; }
  uint32_t position() const { return h_ && h_->file ? ftell(h_->file) : 0; }
  bool seek(uint32_t offset) { return h_ && h_->file && fseek(h_->file, offset, SEEK_SET) == 0; }
  int available() const { return size() > position(); }
  int read() { return h_ && h_->file ? fgetc(h_->file) : -1; }
  size_t read(uint8_t *p, size_t n) { return h_ && h_->file ? fread(p, 1, n, h_->file) : 0; }
  size_t readBytes(char *p, size_t n) { return read(reinterpret_cast<uint8_t *>(p), n); }
  size_t write(const uint8_t *p, size_t n) {
    if (failWriteAfter == 0)
      return 0;
    if (failWriteAfter > 0)
      --failWriteAfter;
    return h_ && h_->file ? fwrite(p, 1, n, h_->file) : 0;
  }
  size_t write(uint8_t value) { return write(&value, 1); }
  void flush() {
    if (h_ && h_->file)
      fflush(h_->file);
  }
  void close() { h_.reset(); }
  const char *path() const { return h_ ? h_->logical.c_str() : ""; }
  const char *name() const { return path(); }
  uint32_t getLastWrite() const {
    return h_ ? uint32_t(std::filesystem::last_write_time(h_->absolute).time_since_epoch().count() /
                         1000000000)
              : 0;
  }
  File openNextFile() {
    if (!h_ || h_->next >= h_->entries.size())
      return {};
    const auto absolute = h_->entries[h_->next++];
    return File(absolute.string(), h_->logical + "/" + absolute.filename().string());
  }
};

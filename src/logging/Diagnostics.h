#pragma once
#include <cstddef>
#include <cstdint>
namespace nova {
class Diagnostics {
public:
  void begin();
  bool clear();
  void record(const char *message);
  void format(char *out, size_t capacity) const;

private:
  struct Entry {
    int64_t epoch = 0;
    uint32_t uptime = 0;
    char text[96]{};
  };
  struct Record {
    uint32_t magic = 0x4e444731, sequence = 0, count = 0;
    Entry entries[16]{};
    uint32_t crc = 0;
  };
  bool save();
  Record history_{};
};
} // namespace nova

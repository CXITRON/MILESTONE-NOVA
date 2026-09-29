#include "Journal.h"
#include "../media/Formats.h"
#include <cstring>
namespace nova {
bool encodeLogJournal(uint8_t *out, size_t capacity, const char *path, uint32_t offset,
                      const uint8_t *data, size_t length) {
  if (!out || !data || !length || length > 1024 || capacity < logJournalHeader + length ||
      !safeStoragePath(path) || strncmp(path, "/logs/", 6) ||
      uint64_t(offset) + length >= 0x80000000)
    return false;
  memset(out, 0, logJournalHeader);
  memcpy(out, "NLJ1", 4);
  strcpy(reinterpret_cast<char *>(out + 4), path);
  write32(out + 132, offset);
  write32(out + 136, length);
  memcpy(out + logJournalHeader, data, length);
  write32(out + 140, ~crcUpdate(crcUpdate(0xffffffff, out, 140), data, length));
  return true;
}
bool validateLogJournal(const uint8_t *p, size_t bytes) {
  if (!p || bytes < logJournalHeader || bytes > logJournalMaximum || memcmp(p, "NLJ1", 4) ||
      !memchr(p + 4, 0, 128))
    return false;
  const char *path = reinterpret_cast<const char *>(p + 4);
  const uint32_t offset = read32(p + 132), length = read32(p + 136);
  return safeStoragePath(path) && !strncmp(path, "/logs/", 6) && length > 0 && length <= 1024 &&
         bytes == logJournalHeader + length && uint64_t(offset) + length < 0x80000000 &&
         read32(p + 140) == ~crcUpdate(crcUpdate(0xffffffff, p, 140), p + logJournalHeader, length);
}
} // namespace nova

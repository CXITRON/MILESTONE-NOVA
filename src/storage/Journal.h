#pragma once
#include <cstddef>
#include <cstdint>
namespace nova {
constexpr size_t logJournalHeader = 144, logJournalMaximum = 1168;
bool encodeLogJournal(uint8_t *out, size_t capacity, const char *path, uint32_t offset,
                      const uint8_t *data, size_t length);
bool validateLogJournal(const uint8_t *data, size_t bytes);
} // namespace nova

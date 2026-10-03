#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
namespace nova {
uint32_t nextCodepoint(std::string_view text, size_t &offset);
size_t cleanUtf8(char *dst, size_t capacity, std::string_view source);
bool validUtf8(std::string_view text);
void trackKey(char (&out)[17], std::string_view artist, std::string_view title,
              std::string_view album, uint32_t durationMs);
bool validTrackKey(std::string_view key);
void sanitizeFilename(char *out, size_t capacity, std::string_view name);
uint32_t crc32(const void *data, size_t length);
// Appends a URL-encoded form value; keeps `out` NUL-terminated and fails before overflowing.
bool appendFormValue(char *out, size_t capacity, size_t &at, const char *value);
} // namespace nova

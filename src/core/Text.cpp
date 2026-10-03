#include "Text.h"
#include <cstdio>
#include <cstring>
namespace nova {
uint32_t nextCodepoint(std::string_view s, size_t &p) {
  if (p >= s.size())
    return 0;
  const uint8_t a = static_cast<uint8_t>(s[p++]);
  if (a < 0x80)
    return a;
  unsigned n;
  uint32_t c, minimum;
  if (a >= 0xC2 && a <= 0xDF) {
    n = 1;
    c = a & 31;
    minimum = 0x80;
  } else if (a >= 0xE0 && a <= 0xEF) {
    n = 2;
    c = a & 15;
    minimum = 0x800;
  } else if (a >= 0xF0 && a <= 0xF4) {
    n = 3;
    c = a & 7;
    minimum = 0x10000;
  } else
    return 0xFFFD;
  if (p + n > s.size())
    return 0xFFFD;
  for (unsigned i = 0; i < n; ++i) {
    const uint8_t b = static_cast<uint8_t>(s[p + i]);
    if ((b & 0xC0) != 0x80)
      return 0xFFFD;
    c = (c << 6) | (b & 63);
  }
  p += n;
  return c < minimum || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF) ? 0xFFFD : c;
}
size_t cleanUtf8(char *out, size_t cap, std::string_view s) {
  if (!cap)
    return 0;
  size_t p = 0, n = 0;
  while (p < s.size()) {
    const size_t start = p;
    const uint32_t cp = nextCodepoint(s, p);
    if (cp == 0 || (cp < 32 && cp != '\n' && cp != '\t'))
      continue;
    const bool invalid = cp == 0xFFFD && s.substr(start, p - start) != "\xEF\xBF\xBD";
    const size_t bytes = invalid ? 1 : p - start;
    if (n + bytes >= cap)
      break;
    if (invalid)
      out[n++] = '?';
    else {
      memcpy(out + n, s.data() + start, bytes);
      n += bytes;
    }
  }
  out[n] = 0;
  return n;
}
bool validUtf8(std::string_view s) {
  size_t p = 0;
  while (p < s.size()) {
    const size_t start = p;
    if (nextCodepoint(s, p) == 0xFFFD && s.substr(start, p - start) != "\xEF\xBF\xBD")
      return false;
  }
  return true;
}
void trackKey(char (&out)[17], std::string_view artist, std::string_view title,
              std::string_view album, uint32_t duration) {
  uint64_t h = 14695981039346656037ULL;
  const auto byte = [&](uint8_t c) {
    h ^= c;
    h *= 1099511628211ULL;
  };
  for (const auto text : {artist, title, album}) {
    const uint32_t len = static_cast<uint32_t>(text.size());
    for (unsigned n = 0; n < 4; ++n)
      byte(static_cast<uint8_t>(len >> (n * 8)));
    for (unsigned char c : text)
      byte(c);
  }
  for (unsigned n = 0; n < 4; ++n)
    byte(static_cast<uint8_t>(duration >> (n * 8)));
  snprintf(out, sizeof(out), "%016llx", static_cast<unsigned long long>(h));
}
bool validTrackKey(std::string_view key) {
  if (key.size() != 16)
    return false;
  for (char c : key)
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
      return false;
  return true;
}
void sanitizeFilename(char *out, size_t cap, std::string_view s) {
  cleanUtf8(out, cap, s);
  for (size_t i = 0; cap && out[i]; ++i)
    if (static_cast<unsigned char>(out[i]) < 32 || strchr("/\\:*?\"<>|", out[i]))
      out[i] = '_';
  if (cap && (!strcmp(out, ".") || !strcmp(out, "..") || !out[0])) {
    cleanUtf8(out, cap, "untitled");
  }
}
uint32_t crc32(const void *data, size_t n) {
  auto p = static_cast<const uint8_t *>(data);
  uint32_t crc = 0xFFFFFFFF;
  while (n--) {
    crc ^= *p++;
    for (int b = 0; b < 8; ++b)
      crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1)));
  }
  return ~crc;
}
bool appendFormValue(char *out, size_t capacity, size_t &at, const char *s) {
  constexpr char hex[] = "0123456789ABCDEF";
  for (const auto *p = reinterpret_cast<const uint8_t *>(s); *p; ++p) {
    const bool plain =
        (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9');
    if (at + (plain ? 1 : 3) >= capacity)
      return false;
    if (plain)
      out[at++] = *p;
    else {
      out[at++] = '%';
      out[at++] = hex[*p >> 4];
      out[at++] = hex[*p & 15];
    }
  }
  out[at] = 0;
  return true;
}
} // namespace nova

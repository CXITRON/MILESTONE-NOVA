#include "Formats.h"
#include "../core/Text.h"
#include <algorithm>
#include <cstring>
namespace nova {
uint16_t read16(const uint8_t *p) { return p[0] | uint16_t(p[1]) << 8; }
uint32_t read32(const uint8_t *p) { return read16(p) | uint32_t(read16(p + 2)) << 16; }
void write16(uint8_t *p, uint16_t n) {
  p[0] = n;
  p[1] = n >> 8;
}
void write32(uint8_t *p, uint32_t n) {
  write16(p, n);
  write16(p + 2, n >> 16);
}
uint32_t crcUpdate(uint32_t s, const uint8_t *p, size_t n) {
  while (n--) {
    s ^= *p++;
    for (unsigned b = 0; b < 8; ++b)
      s = (s >> 1) ^ (0xEDB88320U & (0U - (s & 1)));
  }
  return s;
}
bool mediaHeader(const uint8_t *p, size_t n, uint32_t bytes, MediaInfo &out) {
  if (!p || n < 16 || bytes < 16 || bytes >= 0x80000000U)
    return false;
  MediaInfo m;
  m.width = read16(p + 4);
  m.height = read16(p + 6);
  m.dataOffset = 16;
  if (!memcmp(p, "NVI1", 4)) {
    if (m.width != 160 || m.height != 160 || read32(p + 8) != 51200 || bytes != 51216)
      return false;
    m.format = MediaFormat::Image;
    m.frames = 1;
    m.checksum = read32(p + 12);
  } else if (!memcmp(p, "NVV1", 4) || !memcmp(p, "MVJ1", 4) || !memcmp(p, "NJV1", 4)) {
    m.format = !memcmp(p, "NVV1", 4) ? MediaFormat::RawVideo : MediaFormat::JpegVideo;
    const bool old = !memcmp(p, "MVJ1", 4);
    if (m.width != (old ? 128 : 160) || m.height != m.width || read16(p + 10))
      return false;
    m.fps = read16(p + 8);
    m.frames = read32(p + 12);
    if (!m.fps || m.fps > 30 || !m.frames || m.frames > 648000)
      return false;
    m.duration = uint64_t(m.frames) * 1000 / m.fps;
    if (m.duration > 21600000)
      return false;
    if (m.format == MediaFormat::RawVideo && 16 + uint64_t(m.frames) * (4 + 51200) != bytes)
      return false;
  } else if (!memcmp(p, "MSM1", 4)) {
    if (n < 24 || bytes > 4 * 1024 * 1024 || p[4] != 1 || (p[5] & ~3) || p[6] != 128 ||
        p[7] != 128 || read16(p + 10) > 1)
      return false;
    m.format = MediaFormat::MonoMovie;
    m.width = m.height = 128;
    m.frames = read16(p + 8);
    m.color = read16(p + 10);
    m.duration = read32(p + 12);
    m.payload = read32(p + 16);
    m.checksum = read32(p + 20);
    m.dataOffset = 24;
    m.loop = p[5] & 2;
    if (!m.frames || m.frames > 4096 || (m.frames == 1 ? m.duration != 0 : m.duration == 0) ||
        m.payload != bytes - 24 || bool(p[5] & 1) != (m.frames > 1))
      return false;
  } else if (p[0] == 'B' && p[1] == 'M') {
    if (n < 54 || read32(p + 14) < 40 || read16(p + 26) != 1 || read16(p + 28) != 24 ||
        read32(p + 30) != 0)
      return false;
    const int32_t w = read32(p + 18), h = read32(p + 22);
    if (w < 1 || w > 320 || !h || h < -320 || h > 320)
      return false;
    m.format = MediaFormat::Bitmap;
    m.width = w;
    m.height = h < 0 ? -h : h;
    m.topDown = h < 0;
    m.frames = 1;
    m.rowBytes = (m.width * 3 + 3) & ~3U;
    m.dataOffset = read32(p + 10);
    if (m.dataOffset < 54 || uint64_t(m.dataOffset) + uint64_t(m.rowBytes) * m.height > bytes)
      return false;
  } else
    return false;
  out = m;
  return true;
}
bool deltaFrame(const uint8_t *p, size_t n, uint8_t *frame, size_t cap, bool first) {
  if (!p || n < 5 || !frame || read16(p + 3) != n - 5 || read16(p + 1) > 60000)
    return false;
  if (p[0] == 0) {
    if (n - 5 != cap)
      return false;
    memcpy(frame, p + 5, cap);
    return true;
  }
  if (p[0] != 1 || first)
    return false;
  size_t in = 5, out = 0;
  while (in < n && out < cap) {
    const auto ctl = p[in++];
    const size_t count = (ctl & 127) + 1;
    if (count > cap - out)
      return false;
    if (ctl & 128) {
      if (count > n - in)
        return false;
      for (size_t i = 0; i < count; ++i)
        frame[out + i] ^= p[in + i];
      in += count;
    }
    out += count;
  }
  return in == n && out == cap;
}
void scale565(const uint16_t *in, unsigned w, unsigned h, uint16_t *out, unsigned side) {
  if (!in || !out || !w || !h || !side)
    return;
  for (unsigned y = 0; y < side; ++y)
    for (unsigned x = 0; x < side; ++x)
      out[y * side + x] = in[(uint64_t(y) * h / side) * w + uint64_t(x) * w / side];
}
bool artworkPacket(const uint8_t *p, size_t n, uint16_t *out, unsigned side) {
  if (!p || n != 22704 || memcmp(p, "MAC1", 4) || p[4] != 60 || p[5] != 60 || p[6] != 88 ||
      p[7] != 88 || p[8] != 28 || p[9] != 32 || p[10] != 60 || p[11] != 128)
    return false;
  const uint32_t expected =
      uint32_t(p[12]) << 24 | uint32_t(p[13]) << 16 | uint32_t(p[14]) << 8 | p[15];
  if (crc32(p + 16, n - 16) != expected)
    return false;
  if (out && side)
    for (unsigned y = 0; y < side; ++y)
      for (unsigned x = 0; x < side; ++x) {
        const size_t offset = 7216 + ((y * 88 / side) * 88 + x * 88 / side) * 2;
        out[y * side + x] = uint16_t(p[offset]) << 8 | p[offset + 1];
      }
  return true;
}
bool safeStoragePath(const char *path) {
  if (!path || path[0] != '/' || strlen(path) > 120 || !validUtf8(path))
    return false;
  bool root = false;
  for (const char *p : {"/media", "/artwork", "/lyrics", "/logs", "/update", "/config"}) {
    const size_t n = strlen(p);
    if (!strncmp(path, p, n) && (path[n] == '/' || !path[n]))
      root = true;
  }
  if (!root)
    return false;
  const char *start = path + 1;
  for (const char *p = start;; ++p) {
    if (*p == '/' || !*p) {
      const size_t n = p - start;
      if (!n || (n == 1 && *start == '.') || (n == 2 && start[0] == '.' && start[1] == '.'))
        return false;
      if (!*p)
        break;
      start = p + 1;
    } else if (static_cast<unsigned char>(*p) < 32 || *p == '\\' || *p == ':')
      return false;
  }
  return true;
}
bool mediaExtension(const char *n) {
  const char *p = strrchr(n, '.');
  if (!p)
    return false;
  for (const char *s : {".bmp", ".mvj", ".msm", ".nvi", ".nvv", ".njv"})
    if (!strcmp(p, s))
      return true;
  return false;
}
} // namespace nova

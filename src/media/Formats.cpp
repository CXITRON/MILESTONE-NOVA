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
    if (!novaSide(m.width) || m.height != m.width || read32(p + 8) != rawBytes(m.width) ||
        bytes != 16 + rawBytes(m.width))
      return false;
    m.format = MediaFormat::Image;
    m.frames = 1;
    m.checksum = read32(p + 12);
  } else if (!memcmp(p, "NVV1", 4) || !memcmp(p, "MVJ1", 4) || !memcmp(p, "NJV1", 4)) {
    m.format = !memcmp(p, "NVV1", 4) ? MediaFormat::RawVideo : MediaFormat::JpegVideo;
    const bool old = !memcmp(p, "MVJ1", 4);
    if ((old ? m.width != 128 : !novaSide(m.width)) || m.height != m.width || read16(p + 10))
      return false;
    m.fps = read16(p + 8);
    m.frames = read32(p + 12);
    if (!m.fps || m.fps > 30 || !m.frames || m.frames > 648000)
      return false;
    m.duration = uint64_t(m.frames) * 1000 / m.fps;
    if (m.duration > 21600000)
      return false;
    if (m.format == MediaFormat::RawVideo &&
        16 + uint64_t(m.frames) * (4 + rawBytes(m.width)) != bytes)
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
  if (w == side && h == side) { // The video path: nothing to sample, only a copy.
    memcpy(out, in, size_t(side) * side * sizeof(uint16_t));
    return;
  }
  // 32-bit math only: a 64-bit division per pixel cost ~28 ms for a 240x240 frame on the ESP32-S3.
  // Sizes are small (side and w are at most a few hundred), so y * h and x * w fit comfortably.
  constexpr unsigned maxSide = 512;
  if (side > maxSide)
    return;
  uint16_t columns[maxSide];
  for (unsigned x = 0; x < side; ++x)
    columns[x] = uint16_t(uint32_t(x) * w / side);
  for (unsigned y = 0; y < side; ++y) {
    const uint16_t *row = in + size_t(uint32_t(y) * h / side) * w;
    uint16_t *dst = out + size_t(y) * side;
    for (unsigned x = 0; x < side; ++x)
      dst[x] = row[columns[x]];
  }
}
void resample565(const uint16_t *in, unsigned w, unsigned h, uint16_t *out, unsigned side) {
  if (!in || !out || !w || !h || !side)
    return;
  const auto channel = [](uint16_t p, unsigned c) {
    return c == 0 ? (p >> 11) * 255 / 31 : c == 1 ? ((p >> 5) & 63) * 255 / 63 : (p & 31) * 255 / 31;
  };
  const auto pack = [](unsigned r, unsigned g, unsigned b) {
    return uint16_t((r * 31 + 127) / 255 << 11 | (g * 63 + 127) / 255 << 5 | (b * 31 + 127) / 255);
  };
  for (unsigned y = 0; y < side; ++y)
    for (unsigned x = 0; x < side; ++x) {
      unsigned sum[3]{};
      if (w > side || h > side) {
        // Box average over the source pixels covered by this output pixel.
        const unsigned x0 = x * w / side, x1 = std::max(x0 + 1, (x + 1) * w / side),
                       y0 = y * h / side, y1 = std::max(y0 + 1, (y + 1) * h / side);
        for (unsigned sy = y0; sy < y1; ++sy)
          for (unsigned sx = x0; sx < x1; ++sx)
            for (unsigned c = 0; c < 3; ++c)
              sum[c] += channel(in[sy * w + sx], c);
        const unsigned n = (x1 - x0) * (y1 - y0);
        out[y * side + x] = pack(sum[0] / n, sum[1] / n, sum[2] / n);
        continue;
      }
      // Bilinear with pixel centers aligned; 8-bit fixed-point weights.
      const unsigned fx = std::max<int>(0, int((2 * x + 1) * w * 128 / side) - 128),
                     fy = std::max<int>(0, int((2 * y + 1) * h * 128 / side) - 128);
      const unsigned x0 = std::min(fx >> 8, w - 1), y0 = std::min(fy >> 8, h - 1),
                     x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1), ax = fx & 255,
                     ay = fy & 255;
      for (unsigned c = 0; c < 3; ++c) {
        const unsigned top = channel(in[y0 * w + x0], c) * (256 - ax) + channel(in[y0 * w + x1], c) * ax,
                       bottom =
                           channel(in[y1 * w + x0], c) * (256 - ax) + channel(in[y1 * w + x1], c) * ax;
        sum[c] = (top * (256 - ay) + bottom * ay + 32768) >> 16;
      }
      out[y * side + x] = pack(sum[0], sum[1], sum[2]);
    }
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

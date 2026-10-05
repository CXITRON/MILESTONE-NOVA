#include "MediaDecoder.h"
#include "../core/Text.h"
#include "../media/JpegImage.h"
#include <SD.h>
#include <algorithm>
#include <cstring>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
namespace nova {
namespace {
constexpr size_t encodedBytes = maxJpegFrame + 8, workBytes = 65536;
} // namespace
bool MediaDecoder::begin() {
  encoded_ =
      static_cast<uint8_t *>(heap_caps_malloc(encodedBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  mono_ = static_cast<uint8_t *>(heap_caps_malloc(16384, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  decoded_ = static_cast<uint16_t *>(
      heap_caps_malloc(rawBytes(maxMediaSide), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  work_ = static_cast<uint8_t *>(heap_caps_malloc(workBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  return encoded_ && mono_ && decoded_ && work_;
}
void MediaDecoder::close() {
  file_.close();
  index_.close();
  path_[0] = 0;
  decodedFrame_ = UINT32_MAX;
}
namespace {
// Cheap structural check: SOI, a baseline/progressive SOF with the expected size, and EOI.
bool jpegStructure(const uint8_t *d, size_t n, unsigned w, unsigned h) {
  if (n < 4 || d[0] != 0xFF || d[1] != 0xD8 || d[n - 2] != 0xFF || d[n - 1] != 0xD9)
    return false;
  for (size_t at = 2; at + 4 <= n;) {
    if (d[at] != 0xFF)
      return false;
    const uint8_t marker = d[at + 1];
    if (marker == 0xFF) {
      ++at;
      continue;
    }
    const size_t length = (size_t(d[at + 2]) << 8) | d[at + 3];
    if (marker == 0xC0 || marker == 0xC1 || marker == 0xC2)
      return at + 9 <= n && ((size_t(d[at + 5]) << 8) | d[at + 6]) == h &&
             ((size_t(d[at + 7]) << 8) | d[at + 8]) == w;
    if (marker == 0xDA || length < 2)
      return false;
    at += 2 + length;
  }
  return false;
}
} // namespace
bool MediaDecoder::jpeg(size_t bytes, unsigned w, unsigned h) {
  return w <= maxMediaSide && h <= maxMediaSide &&
         decodeJpeg565(encoded_, bytes, decoded_, w, h, work_, workBytes);
}
bool MediaDecoder::validate(const char *path, std::atomic<uint32_t> &progress,
                            const std::atomic<bool> &cancel, uint32_t *contentCrc) {
  close();
  error_[0] = 0;
  progress = 0;
  if (!encoded_ || !mono_ || !decoded_ || !work_ || !safeStoragePath(path)) {
    strcpy(error_, "Invalid path or memory");
    return false;
  }
  File f = SD.open(path, FILE_READ);
  uint8_t header[64]{};
  MediaInfo m;
  const size_t got = f ? f.read(header, std::min<size_t>(64, f.size())) : 0;
  if (!f || !mediaHeader(header, got, f.size(), m)) {
    strcpy(error_, "Invalid media header");
    return false;
  }
  const uint32_t bytes = f.size();
  if (m.format == MediaFormat::Bitmap) {
    if (contentCrc) {
      uint32_t state = 0xffffffff;
      f.seek(0);
      while (f.position() < bytes && !cancel) {
        const size_t n = std::min<uint32_t>(32768, bytes - f.position());
        if (f.read(encoded_, n) != n)
          return false;
        state = crcUpdate(state, encoded_, n);
        vTaskDelay(1);
      }
      if (cancel)
        return false;
      *contentCrc = ~state;
    }
    progress = 100;
    return true;
  }
  uint32_t content = crcUpdate(0xffffffff, header, m.dataOffset);
  f.seek(m.dataOffset);
  char sidecar[144], temp[144];
  snprintf(sidecar, sizeof(sidecar), "%s.nix", path);
  snprintf(temp, sizeof(temp), "%s.nix.tmp", path);
  const bool indexed = m.format == MediaFormat::JpegVideo || m.format == MediaFormat::MonoMovie;
  File index;
  if (indexed) {
    SD.remove(temp);
    index = SD.open(temp, FILE_WRITE);
    uint8_t h[16]{};
    memcpy(h, "NIX1", 4);
    write32(h + 4, bytes);
    write32(h + 8, m.frames);
    if (!index || index.write(h, 16) != 16)
      return false;
  }
  uint32_t state = 0xFFFFFFFF, duration = 0;
  bool ok = true;
  for (uint32_t frame = 0; frame < m.frames && ok && !cancel; ++frame) {
    const uint32_t at = f.position();
    if (indexed) {
      uint8_t item[8];
      write32(item, at);
      write32(item + 4, duration);
      ok = index.write(item, 8) == 8;
    }
    if (m.format == MediaFormat::Image || m.format == MediaFormat::RawVideo) {
      uint8_t crc[4]{};
      const uint32_t expected =
          m.format == MediaFormat::Image ? m.checksum : (f.read(crc, 4) == 4 ? read32(crc) : 0);
      const uint32_t payload = rawBytes(m.width);
      ok = ok && f.read(reinterpret_cast<uint8_t *>(decoded_), payload) == payload &&
           crc32(decoded_, payload) == expected;
      if (contentCrc && ok) {
        if (m.format == MediaFormat::RawVideo)
          content = crcUpdate(content, crc, sizeof(crc));
        content = crcUpdate(content, reinterpret_cast<const uint8_t *>(decoded_), payload);
      }
    } else if (m.format == MediaFormat::JpegVideo) {
      uint8_t h[8]{};
      ok = ok && f.read(h, 8) == 8;
      const uint32_t n = read32(h);
      ok = ok && n >= 4 && n <= maxJpegFrame && n <= f.size() - f.position() && f.read(encoded_, n) == n &&
           crc32(encoded_, n) == read32(h + 4) &&
           // Every frame has a CRC and a structure check; decoding all of them dominated the
           // validation time, so only a sample (first frame, then every 32nd) is fully decoded.
           jpegStructure(encoded_, n, m.width, m.height) &&
           ((frame & 31) != 0 || jpeg(n, m.width, m.height));
      if (contentCrc && ok) {
        content = crcUpdate(content, h, sizeof(h));
        content = crcUpdate(content, encoded_, n);
      }
    } else {
      ok = ok && f.read(encoded_, 5) == 5;
      const uint16_t n = read16(encoded_ + 3);
      ok = ok && n <= 16640 && f.read(encoded_ + 5, n) == n &&
           deltaFrame(encoded_, n + 5, mono_, m.color ? 16384 : 2048, frame == 0);
      if (ok) {
        state = crcUpdate(state, encoded_, n + 5);
        if (contentCrc)
          content = crcUpdate(content, encoded_, n + 5);
        duration += read16(encoded_ + 1);
      }
    }
    progress = bytes ? uint64_t(f.position()) * 100 / bytes : 0;
    vTaskDelay(1);
  }
  ok = ok && !cancel && f.position() == bytes;
  if (m.format == MediaFormat::MonoMovie)
    ok = ok && ~state == m.checksum && duration == m.duration;
  index.flush();
  index.close();
  f.close();
  if (indexed) {
    if (ok) {
      SD.remove(sidecar);
      ok = SD.rename(temp, sidecar);
    } else
      SD.remove(temp);
  }
  if (!ok)
    strcpy(error_, cancel ? "Cancelled" : "Frame/CRC/JPEG validation failed");
  progress = ok ? 100 : 0;
  if (ok && contentCrc)
    *contentCrc = ~content;
  return ok;
}
bool MediaDecoder::open(const char *path) {
  if (!strcmp(path_, path) && file_)
    return true;
  close();
  file_ = SD.open(path, FILE_READ);
  uint8_t h[64]{};
  const size_t got = file_ ? file_.read(h, std::min<size_t>(64, file_.size())) : 0;
  if (!file_ || !mediaHeader(h, got, file_.size(), info_)) {
    close();
    return false;
  }
  if (info_.format == MediaFormat::JpegVideo || info_.format == MediaFormat::MonoMovie) {
    char idx[144];
    snprintf(idx, sizeof(idx), "%s.nix", path);
    index_ = SD.open(idx, FILE_READ);
    uint8_t head[16]{};
    if (!index_ || index_.read(head, 16) != 16 || memcmp(head, "NIX1", 4) ||
        read32(head + 4) != file_.size() || read32(head + 8) != info_.frames ||
        index_.size() != 16 + uint64_t(info_.frames) * 8) {
      close();
      return false;
    }
  }
  strcpy(path_, path);
  return true;
}
bool MediaDecoder::record(uint32_t n) {
  uint8_t index[8];
  if (!index_.seek(16 + uint64_t(n) * 8) || index_.read(index, 8) != 8 ||
      !file_.seek(read32(index)))
    return false;
  if (info_.format == MediaFormat::JpegVideo) {
    uint8_t h[8]{};
    if (file_.read(h, 8) != 8)
      return false;
    const uint32_t bytes = read32(h);
    return bytes >= 4 && bytes <= maxJpegFrame && file_.read(encoded_, bytes) == bytes &&
           crc32(encoded_, bytes) == read32(h + 4) && jpeg(bytes, info_.width, info_.height);
  }
  if (file_.read(encoded_, 5) != 5)
    return false;
  const uint16_t bytes = read16(encoded_ + 3);
  return bytes <= 16640 && file_.read(encoded_ + 5, bytes) == bytes &&
         deltaFrame(encoded_, bytes + 5, mono_, info_.color ? 16384 : 2048, n == 0);
}
bool MediaDecoder::bitmap(uint16_t *out, unsigned side) {
  for (unsigned y = 0; y < side; ++y) {
    unsigned sy = uint64_t(y) * info_.height / side;
    if (!info_.topDown)
      sy = info_.height - 1 - sy;
    if (!file_.seek(info_.dataOffset + sy * info_.rowBytes) ||
        file_.read(encoded_, info_.rowBytes) != info_.rowBytes)
      return false;
    for (unsigned x = 0; x < side; ++x) {
      const auto *p = encoded_ + (uint64_t(x) * info_.width / side) * 3;
      out[y * side + x] = uint16_t(p[2] >> 3) << 11 | uint16_t(p[1] >> 2) << 5 | (p[0] >> 3);
    }
  }
  return true;
}
void MediaDecoder::monoToPixels(uint16_t *out, unsigned side) {
  for (unsigned y = 0; y < side; ++y)
    for (unsigned x = 0; x < side; ++x) {
      const unsigned at = (y * 128 / side) * 128 + x * 128 / side;
      if (info_.color) {
        const auto c = mono_[at];
        const unsigned r = (c >> 5) * 31 / 7, g = ((c >> 2) & 7) * 63 / 7, b = (c & 3) * 31 / 3;
        out[y * side + x] = r << 11 | g << 5 | b;
      } else
        out[y * side + x] = (mono_[at / 8] & (1U << (7 - at % 8))) ? 0xFFFF : 0;
    }
}
bool MediaDecoder::frame(const char *path, uint32_t pos, uint16_t *out, uint32_t &duration,
                         unsigned side) {
  if (!out || !side || side > maxMediaSide || !open(path))
    return false;
  duration = info_.frames > 1 ? info_.duration : 0;
  if (info_.format == MediaFormat::Bitmap)
    return bitmap(out, side);
  if (info_.format == MediaFormat::Image || info_.format == MediaFormat::RawVideo) {
    uint32_t frame =
        info_.fps ? std::min<uint64_t>(uint64_t(pos) * info_.fps / 1000, info_.frames - 1) : 0;
    uint32_t expected = info_.checksum;
    uint8_t crc[4];
    const uint32_t payload = rawBytes(info_.width);
    const bool raw = info_.format == MediaFormat::RawVideo;
    if (!file_.seek(16 + (raw ? uint64_t(frame) * (4 + payload) : 0)))
      return false;
    if (raw) {
      if (file_.read(crc, 4) != 4)
        return false;
      expected = read32(crc);
    }
    decodedFrame_ = UINT32_MAX;
    if (file_.read(reinterpret_cast<uint8_t *>(decoded_), payload) != payload ||
        crc32(decoded_, payload) != expected)
      return false;
    if (info_.width == side)
      memcpy(out, decoded_, payload);
    else if (raw)
      scale565(decoded_, info_.width, info_.height, out, side);
    else
      resample565(decoded_, info_.width, info_.height, out, side);
    return true;
  }
  if (info_.format == MediaFormat::JpegVideo) {
    const uint32_t target = std::min<uint64_t>(uint64_t(pos) * info_.fps / 1000, info_.frames - 1);
    if (target != decodedFrame_ && !record(target))
      return false;
    decodedFrame_ = target;
    scale565(decoded_, info_.width, info_.height, out, side);
    return true;
  }
  uint32_t low = 0, high = info_.frames;
  uint8_t idx[8];
  while (low < high) {
    const uint32_t mid = (low + high) / 2;
    if (!index_.seek(16 + uint64_t(mid) * 8) || index_.read(idx, 8) != 8)
      return false;
    if (read32(idx + 4) <= pos)
      low = mid + 1;
    else
      high = mid;
  }
  const uint32_t target = low ? low - 1 : 0;
  uint32_t start = decodedFrame_ == UINT32_MAX || target < decodedFrame_ ? 0 : decodedFrame_ + 1;
  for (uint32_t n = start; n <= target; ++n) {
    if (!record(n))
      return false;
    if ((n & 15) == 0)
      vTaskDelay(1);
  }
  decodedFrame_ = target;
  monoToPixels(out, side);
  return true;
}
} // namespace nova

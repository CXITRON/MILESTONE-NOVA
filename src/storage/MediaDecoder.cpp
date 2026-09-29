#include "MediaDecoder.h"
#include "../core/Text.h"
#include <SD.h>
#include <algorithm>
#include <cstring>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <jpeg_decoder.h>
namespace nova {
bool MediaDecoder::begin() {
  encoded_ = static_cast<uint8_t *>(heap_caps_malloc(32776, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  mono_ = static_cast<uint8_t *>(heap_caps_malloc(16384, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  decoded_ = static_cast<uint16_t *>(heap_caps_malloc(51200, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  work_ = static_cast<uint8_t *>(heap_caps_malloc(65536, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  return encoded_ && mono_ && decoded_ && work_;
}
void MediaDecoder::close() {
  file_.close();
  index_.close();
  path_[0] = 0;
  decodedFrame_ = UINT32_MAX;
}
bool MediaDecoder::jpeg(size_t bytes, unsigned w, unsigned h) {
  esp_jpeg_image_cfg_t cfg{};
  cfg.indata = encoded_;
  cfg.indata_size = bytes;
  cfg.outbuf = reinterpret_cast<uint8_t *>(decoded_);
  cfg.outbuf_size = 51200;
  cfg.out_format = JPEG_IMAGE_FORMAT_RGB565;
  cfg.advanced.working_buffer = work_;
  cfg.advanced.working_buffer_size = 65536;
  esp_jpeg_image_output_t result{};
  if (esp_jpeg_get_image_info(&cfg, &result) != ESP_OK || result.width != w || result.height != h ||
      result.output_len > 51200)
    return false;
  return esp_jpeg_decode(&cfg, &result) == ESP_OK && result.width == w && result.height == h;
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
      ok = ok && f.read(reinterpret_cast<uint8_t *>(decoded_), 51200) == 51200 &&
           crc32(decoded_, 51200) == expected;
      if (contentCrc && ok) {
        if (m.format == MediaFormat::RawVideo)
          content = crcUpdate(content, crc, sizeof(crc));
        content = crcUpdate(content, reinterpret_cast<const uint8_t *>(decoded_), 51200);
      }
    } else if (m.format == MediaFormat::JpegVideo) {
      uint8_t h[8]{};
      ok = ok && f.read(h, 8) == 8;
      const uint32_t n = read32(h);
      ok = ok && n >= 4 && n <= 32768 && n <= f.size() - f.position() && f.read(encoded_, n) == n &&
           crc32(encoded_, n) == read32(h + 4) && jpeg(n, m.width, m.height);
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
    return bytes >= 4 && bytes <= 32768 && file_.read(encoded_, bytes) == bytes &&
           crc32(encoded_, bytes) == read32(h + 4) && jpeg(bytes, info_.width, info_.height);
  }
  if (file_.read(encoded_, 5) != 5)
    return false;
  const uint16_t bytes = read16(encoded_ + 3);
  return bytes <= 16640 && file_.read(encoded_ + 5, bytes) == bytes &&
         deltaFrame(encoded_, bytes + 5, mono_, info_.color ? 16384 : 2048, n == 0);
}
bool MediaDecoder::bitmap(uint16_t *out) {
  for (unsigned y = 0; y < 160; ++y) {
    unsigned sy = uint64_t(y) * info_.height / 160;
    if (!info_.topDown)
      sy = info_.height - 1 - sy;
    if (!file_.seek(info_.dataOffset + sy * info_.rowBytes) ||
        file_.read(encoded_, info_.rowBytes) != info_.rowBytes)
      return false;
    for (unsigned x = 0; x < 160; ++x) {
      const auto *p = encoded_ + (uint64_t(x) * info_.width / 160) * 3;
      out[y * 160 + x] = uint16_t(p[2] >> 3) << 11 | uint16_t(p[1] >> 2) << 5 | (p[0] >> 3);
    }
  }
  return true;
}
void MediaDecoder::monoToPixels(uint16_t *out) {
  for (unsigned y = 0; y < 160; ++y)
    for (unsigned x = 0; x < 160; ++x) {
      const unsigned at = (y * 128 / 160) * 128 + x * 128 / 160;
      if (info_.color) {
        const auto c = mono_[at];
        const unsigned r = (c >> 5) * 31 / 7, g = ((c >> 2) & 7) * 63 / 7, b = (c & 3) * 31 / 3;
        out[y * 160 + x] = r << 11 | g << 5 | b;
      } else
        out[y * 160 + x] = (mono_[at / 8] & (1U << (7 - at % 8))) ? 0xFFFF : 0;
    }
}
bool MediaDecoder::frame(const char *path, uint32_t pos, uint16_t *out, uint32_t &duration) {
  if (!open(path))
    return false;
  duration = info_.frames > 1 ? info_.duration : 0;
  if (info_.format == MediaFormat::Bitmap)
    return bitmap(out);
  if (info_.format == MediaFormat::Image || info_.format == MediaFormat::RawVideo) {
    uint32_t frame =
        info_.fps ? std::min<uint64_t>(uint64_t(pos) * info_.fps / 1000, info_.frames - 1) : 0;
    uint32_t expected = info_.checksum;
    uint8_t crc[4];
    if (!file_.seek(16 + uint64_t(frame) * (4 + 51200)))
      return false;
    if (info_.format == MediaFormat::RawVideo) {
      if (file_.read(crc, 4) != 4)
        return false;
      expected = read32(crc);
    }
    return file_.read(reinterpret_cast<uint8_t *>(out), 51200) == 51200 &&
           crc32(out, 51200) == expected;
  }
  if (info_.format == MediaFormat::JpegVideo) {
    const uint32_t target = std::min<uint64_t>(uint64_t(pos) * info_.fps / 1000, info_.frames - 1);
    if (target != decodedFrame_ && !record(target))
      return false;
    decodedFrame_ = target;
    scale565(decoded_, info_.width, info_.height, out, 160);
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
  monoToPixels(out);
  return true;
}
} // namespace nova

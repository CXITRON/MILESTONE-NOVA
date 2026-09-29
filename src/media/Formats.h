#pragma once
#include <cstddef>
#include <cstdint>
namespace nova {
enum class MediaFormat : uint8_t { Unknown, Image, RawVideo, JpegVideo, Bitmap, MonoMovie };
struct MediaInfo {
  MediaFormat format = MediaFormat::Unknown;
  uint16_t width = 0, height = 0, fps = 0;
  uint32_t frames = 0, duration = 0, payload = 0, checksum = 0, dataOffset = 0, rowBytes = 0;
  bool color = false, topDown = false, loop = false;
};
uint16_t read16(const uint8_t *p);
uint32_t read32(const uint8_t *p);
void write16(uint8_t *p, uint16_t n);
void write32(uint8_t *p, uint32_t n);
bool mediaHeader(const uint8_t *data, size_t bytes, uint32_t fileBytes, MediaInfo &out);
bool deltaFrame(const uint8_t *data, size_t size, uint8_t *frame, size_t capacity, bool first);
bool artworkPacket(const uint8_t *data, size_t size, uint16_t *pixels, unsigned side);
void scale565(const uint16_t *input, unsigned width, unsigned height, uint16_t *output,
              unsigned side);
uint32_t crcUpdate(uint32_t state, const uint8_t *data, size_t size);
bool safeStoragePath(const char *path);
bool mediaExtension(const char *name);
} // namespace nova

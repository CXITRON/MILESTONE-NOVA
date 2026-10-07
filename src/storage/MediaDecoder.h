#pragma once
#include "../media/Formats.h"
#include <FS.h>
#include <atomic>
namespace nova {
// Used exclusively by the SD owner task. No renderer/network code touches its files.
class MediaDecoder {
public:
  bool begin();
  void close();
  bool validate(const char *path, std::atomic<uint32_t> &progress, const std::atomic<bool> &cancel,
                uint32_t *contentCrc = nullptr);
  // Writes a side x side RGB565 frame; stills are smoothed, video frames use nearest sampling.
  bool frame(const char *path, uint32_t position, uint16_t *pixels, uint32_t &duration,
             unsigned side);
  const char *error() const { return error_; }
  // Microseconds the latest video frame spent on SD reads (index, header, data, CRC) and JPEG decoding.
  uint32_t readUs() const { return readUs_; }
  uint32_t jpegUs() const { return jpegUs_; }

private:
  uint32_t readUs_ = 0, jpegUs_ = 0;
  bool open(const char *path);
  bool jpeg(size_t bytes, unsigned width, unsigned height, uint16_t *output = nullptr);
  bool record(uint32_t index, uint16_t *output = nullptr);
  bool bitmap(uint16_t *pixels, unsigned side);
  void monoToPixels(uint16_t *pixels, unsigned side);
  File file_, index_;
  MediaInfo info_;
  char path_[128]{}, error_[80]{};
  uint8_t *encoded_ = nullptr, *mono_ = nullptr, *work_ = nullptr;
  uint16_t *decoded_ = nullptr;
  uint32_t decodedFrame_ = UINT32_MAX;
};
} // namespace nova

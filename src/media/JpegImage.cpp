#include "JpegImage.h"
#include <jpeg_decoder.h>
namespace nova {
bool decodeJpeg565(const uint8_t *jpeg, size_t bytes, uint16_t *pixels, unsigned w, unsigned h,
                   uint8_t *work, size_t workBytes) {
  if (!jpeg || !bytes || !pixels || !work || !w || !h)
    return false;
  const size_t capacity = size_t(w) * h * 2;
  esp_jpeg_image_cfg_t cfg{};
  cfg.indata = const_cast<uint8_t *>(jpeg);
  cfg.indata_size = bytes;
  cfg.outbuf = reinterpret_cast<uint8_t *>(pixels);
  cfg.outbuf_size = capacity;
  cfg.out_format = JPEG_IMAGE_FORMAT_RGB565;
  cfg.advanced.working_buffer = work;
  cfg.advanced.working_buffer_size = workBytes;
  esp_jpeg_image_output_t result{};
  if (esp_jpeg_get_image_info(&cfg, &result) != ESP_OK || result.width != w ||
      result.height != h || result.output_len > capacity)
    return false;
  return esp_jpeg_decode(&cfg, &result) == ESP_OK && result.width == w && result.height == h;
}
} // namespace nova

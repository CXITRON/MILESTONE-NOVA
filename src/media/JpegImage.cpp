#include "JpegImage.h"
#include <jpeg_decoder.h>
#if defined(ARDUINO) || defined(NOVA_FAST_JPEG)
#include <JPEGDEC.h>
#include <new>
#include <climits>
#endif
namespace nova {
bool decodeJpeg565(const uint8_t *jpeg, size_t bytes, uint16_t *pixels, unsigned w, unsigned h,
                   uint8_t *work, size_t workBytes, bool fast) {
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
#if defined(ARDUINO) || defined(NOVA_FAST_JPEG)
  // Never put the decoder's large state on the worker stack. SIMD needs aligned buffers.
  // Unsupported input, allocation/alignment constraints and decode failure use esp_jpeg.
  if (fast && w % 16 == 0 && h % 16 == 0 && bytes <= INT_MAX && workBytes >= sizeof(JPEGDEC) &&
      !(reinterpret_cast<uintptr_t>(jpeg) & 15) &&
      !(reinterpret_cast<uintptr_t>(pixels) & 15) &&
      !(reinterpret_cast<uintptr_t>(work) & 15)) {
    auto *decoder = new (work) JPEGDEC;
    const bool opened = decoder->openRAM(const_cast<uint8_t *>(jpeg), int(bytes),
                                        [](JPEGDRAW *) { return 1; });
    bool ok = false;
    if (opened && decoder->getJPEGType() == JPEG_MODE_BASELINE && decoder->getWidth() == int(w) && decoder->getHeight() == int(h)) {
      decoder->setPixelType(RGB565_LITTLE_ENDIAN);
      decoder->setFramebuffer(pixels);
      ok = decoder->decode(0, 0, 0) == 1;
    }
    decoder->close();
    decoder->~JPEGDEC();
    if (ok) return true;
  }
#else
  (void)fast;
#endif
  return esp_jpeg_decode(&cfg, &result) == ESP_OK && result.width == w && result.height == h;
}
} // namespace nova

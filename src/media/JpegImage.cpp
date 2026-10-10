#include "JpegImage.h"
#include <jpeg_decoder.h>
#if defined(ARDUINO) || defined(NOVA_FAST_JPEG)
#include <JPEGDEC.h>
#include <new>
#include <climits>
#endif
namespace nova {
#if defined(ARDUINO) || defined(NOVA_FAST_JPEG)
namespace {
// JPEGDEC indexes fixed tables with header values (Huffman table numbers, MCU size) and does not
// check them: a one-byte header edit gives an out-of-range index or a division by zero. Only
// headers inside what it supports reach it; everything else uses esp_jpeg.
bool fastJpegHeader(const uint8_t *d, size_t n) {
  if (n < 4 || d[0] != 0xFF || d[1] != 0xD8)
    return false;
  bool frame = false;
  unsigned components = 0;
  for (size_t at = 2; at + 4 <= n;) {
    if (d[at] != 0xFF)
      return false;
    const uint8_t marker = d[at + 1];
    if (marker == 0xFF) {
      ++at;
      continue;
    }
    const size_t length = (size_t(d[at + 2]) << 8) | d[at + 3];
    if (length < 2 || at + 2 + length > n)
      return false;
    const uint8_t *body = d + at + 4;
    const size_t bytes = length - 2;
    if (marker == 0xC0) {
      if (bytes < 6 || body[0] != 8)
        return false;
      components = body[5];
      if ((components != 1 && components != 3) || bytes < 6 + components * 3)
        return false;
      for (unsigned i = 0; i < components; ++i) {
        const uint8_t sampling = body[6 + i * 3 + 1], quant = body[6 + i * 3 + 2];
        const bool luma = i == 0;
        if (quant > 3 || (luma ? sampling != 0x11 && sampling != 0x21 && sampling != 0x12 && sampling != 0x22
                               : sampling != 0x11))
          return false;
      }
      frame = true;
    } else if (marker == 0xC4) {
      for (size_t p = 0; p < bytes;) {
        if (p + 17 > bytes || (body[p] & 0xEE))
          return false;
        size_t symbols = 0;
        for (unsigned i = 1; i <= 16; ++i)
          symbols += body[p + i];
        if (symbols > bytes - p - 17)
          return false;
        p += 17 + symbols;
      }
    } else if (marker == 0xDB) {
      for (size_t p = 0; p < bytes;) {
        const size_t entry = 1 + (body[p] >> 4 ? 128 : 64);
        if ((body[p] & 15) > 3 || p + entry > bytes)
          return false;
        p += entry;
      }
    } else if (marker == 0xDA) {
      if (!frame || bytes < 1 + components * 2 + 3 || body[0] != components)
        return false;
      for (unsigned i = 0; i < components; ++i)
        if (body[1 + i * 2 + 1] & 0xEE)
          return false;
      return true;
    } else if (marker >= 0xC1 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
      return false; // extended, progressive, lossless or arithmetic: not handled by the fast path
    }
    at += 2 + length;
  }
  return false;
}
} // namespace
#endif
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
  if (fast && fastJpegHeader(jpeg, bytes) && w % 16 == 0 && h % 16 == 0 && bytes <= INT_MAX && workBytes >= sizeof(JPEGDEC) &&
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

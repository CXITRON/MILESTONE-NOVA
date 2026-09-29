// Host adapter substitutes libjpeg for the ESP-IDF decoder; SD/index/CRC code is unchanged.
#include "jpeg_decoder.h"
#include <csetjmp>
#include <cstdio>
#include <jpeglib.h>
struct Error {
  jpeg_error_mgr manager;
  jmp_buf jump;
};
static void failure(j_common_ptr c) { longjmp(reinterpret_cast<Error *>(c->err)->jump, 1); }
static int decode(esp_jpeg_image_cfg_t *cfg, esp_jpeg_image_output_t *out, bool pixels) {
  jpeg_decompress_struct decoder{};
  Error error{};
  decoder.err = jpeg_std_error(&error.manager);
  error.manager.error_exit = failure;
  if (setjmp(error.jump)) {
    jpeg_destroy_decompress(&decoder);
    return -1;
  }
  jpeg_create_decompress(&decoder);
  jpeg_mem_src(&decoder, cfg->indata, cfg->indata_size);
  jpeg_read_header(&decoder, TRUE);
  out->width = decoder.image_width;
  out->height = decoder.image_height;
  out->output_len = out->width * out->height * 2;
  if (!pixels) {
    jpeg_destroy_decompress(&decoder);
    return 0;
  }
  if (out->output_len > cfg->outbuf_size || out->width > 320) {
    jpeg_destroy_decompress(&decoder);
    return -1;
  }
  decoder.out_color_space = JCS_RGB;
  jpeg_start_decompress(&decoder);
  uint8_t row[960];
  auto *dest = reinterpret_cast<uint16_t *>(cfg->outbuf);
  while (decoder.output_scanline < decoder.output_height) {
    const unsigned y = decoder.output_scanline;
    JSAMPROW line = row;
    jpeg_read_scanlines(&decoder, &line, 1);
    for (unsigned x = 0; x < out->width; ++x)
      dest[y * out->width + x] =
          ((row[x * 3] >> 3) << 11) | ((row[x * 3 + 1] >> 2) << 5) | (row[x * 3 + 2] >> 3);
  }
  jpeg_finish_decompress(&decoder);
  jpeg_destroy_decompress(&decoder);
  return 0;
}
int esp_jpeg_get_image_info(esp_jpeg_image_cfg_t *c, esp_jpeg_image_output_t *o) {
  return decode(c, o, false);
}
int esp_jpeg_decode(esp_jpeg_image_cfg_t *c, esp_jpeg_image_output_t *o) {
  return decode(c, o, true);
}

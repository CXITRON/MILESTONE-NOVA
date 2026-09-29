#pragma once
#include <cstddef>
#include <cstdint>
inline constexpr int ESP_OK = 0, JPEG_IMAGE_FORMAT_RGB565 = 0;
struct esp_jpeg_image_cfg_t {
  uint8_t *indata = nullptr, *outbuf = nullptr;
  uint32_t indata_size = 0, outbuf_size = 0;
  int out_format = 0;
  struct {
    void *working_buffer = nullptr;
    size_t working_buffer_size = 0;
  } advanced;
};
struct esp_jpeg_image_output_t {
  uint16_t width = 0, height = 0;
  size_t output_len = 0;
};
int esp_jpeg_get_image_info(esp_jpeg_image_cfg_t *, esp_jpeg_image_output_t *);
int esp_jpeg_decode(esp_jpeg_image_cfg_t *, esp_jpeg_image_output_t *);

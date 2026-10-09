# JPEGDEC 1.8.4

Upstream: https://github.com/bitbank2/JPEGDEC
Pinned source commit: 86282979224c8a32fd51e091ed5a35b0c699a52b.
Apache-2.0; see LICENSE and retained source copyright notices.
Source copied from the existing local benchmark checkout. NOVA portability patches: memcpy-based unaligned integer loads; guard negative Huffman table
shift counts; memmove for overlapping parser buffer compaction. Retained source copyright applies.
The library.properties dependency on bb_spi_lcd was removed because NOVA uses only the RAM
framebuffer decoder, not that display integration. Tests exercise the real portable decoder.
ESP32-S3 input/output and work buffers must be 16-byte aligned; NOVA falls back to esp_jpeg
on unsupported input, bad alignment or decode failure. Progressive input and dimensions not divisible by 16 use the old decoder. The SIMD framebuffer
path can write a complete MCU past the edge for partial dimensions; it is deliberately restricted.
The legacy header parser checks dimensions/capacity before JPEGDEC is attempted.

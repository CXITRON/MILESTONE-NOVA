#pragma once
#include <cstddef>
#include <cstdint>
namespace nova {
// Decodes a baseline JPEG of exactly width x height into RGB565 display pixels.
// `work` is the decoder scratch buffer; callers own all buffers for the duration of the call.
bool decodeJpeg565(const uint8_t *jpeg, size_t bytes, uint16_t *pixels, unsigned width,
                   unsigned height, uint8_t *work, size_t workBytes);
} // namespace nova

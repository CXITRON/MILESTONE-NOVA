// The prebuilt Arduino core allocates every mbedTLS block from internal RAM only, and a TLS
// session needs two ~16 KiB record buffers. Internal RAM is fragmented by Wi-Fi/BLE, so
// handshakes failed with MBEDTLS_ERR_SSL_ALLOC_FAILED even with plenty of free memory.
// The link step redirects esp_mbedtls_mem_calloc here (see scripts/build/firmware.sh): large blocks
// come from PSRAM first, small ones stay in fast internal RAM, and either falls back to the other.
#include <cstddef>
#include <esp_heap_caps.h>
extern "C" void *__wrap_esp_mbedtls_mem_calloc(size_t count, size_t size) {
  constexpr size_t externalFrom = 2048;
  const uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  const uint32_t external = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  const bool large = size && count >= externalFrom / size + (externalFrom % size != 0);
  void *block = heap_caps_calloc(count, size, large ? external : internal);
  return block ? block : heap_caps_calloc(count, size, large ? internal : external);
}

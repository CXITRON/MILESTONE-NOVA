#include "HelperProtocol.h"
#include "../core/Text.h"
#include <cstring>
namespace nova {
namespace {
uint32_t le32(const uint8_t *p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
} // namespace
void HelperDecoder::reset() {
  track_ = Track{};
  fields_ = 0;
  playing_ = false;
  position_ = 0;
}
bool HelperDecoder::accept(const uint8_t *p, size_t n, uint32_t now) {
  if (n < 6 || n > 246 || p[0] != 1) {
    reset();
    return false;
  }
  const uint8_t type = p[1];
  const uint32_t id = le32(p + 2);
  p += 6;
  n -= 6;
  if (type == 1) {
    reset();
    transaction_ = id;
    started_ = now;
    if (n && (n != 16 || !validTrackKey(std::string_view(reinterpret_cast<const char *>(p), n))))
      return false;
    if (n)
      memcpy(track_.key, p, n);
    fields_ = 1;
    return false;
  }
  if (!fields_ || id != transaction_ || now - started_ > 5000) {
    reset();
    return false;
  }
  if (type >= 2 && type <= 4) {
    char *dest = type == 2 ? track_.title : type == 3 ? track_.artist : track_.album;
    const size_t used = strlen(dest);
    if (used + n > 240) {
      reset();
      return false;
    }
    cleanUtf8(dest + used, 241 - used, std::string_view(reinterpret_cast<const char *>(p), n));
    fields_ |= 1U << (type - 1);
  } else if (type == 5) {
    if (n != 9 || p[8] > 1) {
      reset();
      return false;
    }
    position_ = le32(p);
    track_.durationMs = le32(p + 4);
    playing_ = p[8];
    fields_ |= 16;
  } else if (type == 6 && n == 0 && fields_ == 31) {
    fields_ = 0;
    if (!track_.key[0])
      trackKey(track_.key, track_.artist, track_.title, track_.album, track_.durationMs);
    return true;
  } else
    reset();
  return false;
}
} // namespace nova

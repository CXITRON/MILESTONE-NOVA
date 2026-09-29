#include "AmsDecoder.h"
#include "../core/Text.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
namespace nova {
void AmsDecoder::reset() {
  pending_ = Track{};
  dirty_ = positionAvailable_ = playing_ = false;
}
void AmsDecoder::accept(const uint8_t *data, size_t size, uint32_t at, MediaSession &session) {
  if (size < 3 || size > 246)
    return;
  char value[244];
  cleanUtf8(value, sizeof(value), {reinterpret_cast<const char *>(data + 3), size - 3});
  if (data[0] == 2) {
    switch (data[1]) {
    case 0:
      cleanUtf8(pending_.artist, sizeof(pending_.artist), value);
      break;
    case 1:
      cleanUtf8(pending_.album, sizeof(pending_.album), value);
      break;
    case 2:
      cleanUtf8(pending_.title, sizeof(pending_.title), value);
      break;
    case 3: {
      char *end;
      const double seconds = strtod(value, &end);
      if (end == value || *end || !std::isfinite(seconds) || seconds < 0 || seconds > 86400)
        return;
      pending_.durationMs = static_cast<uint32_t>(seconds * 1000);
      break;
    }
    default:
      return;
    }
    dirty_ = true;
    changedAt_ = at;
  } else if ((data[0] == 0 && data[1] != 1) || data[0] == 1) {
    auto info = session.player();
    if (data[0] == 0 && data[1] == 0)
      cleanUtf8(info.name, sizeof(info.name), value);
    else {
      char *end = nullptr;
      const double n = strtod(value, &end);
      if (end == value || *end || !std::isfinite(n) || n < 0)
        return;
      if (data[0] == 0 && data[1] == 2) {
        if (n > 1)
          return;
        info.volume = n;
        info.volumeKnown = true;
      } else if (data[0] == 1) {
        if (n > UINT32_MAX || floor(n) != n)
          return;
        switch (data[1]) {
        case 0:
          info.queueIndex = n;
          break;
        case 1:
          info.queueCount = n;
          break;
        case 2:
          if (n > 2)
            return;
          info.shuffle = n;
          break;
        case 3:
          if (n > 2)
            return;
          info.repeat = n;
          break;
        default:
          return;
        }
      } else
        return;
    }
    session.player(info);
  } else if (data[0] == 0 && data[1] == 1) {
    unsigned state;
    float rate, seconds;
    char trailing;
    if (sscanf(value, "%u,%f,%f%c", &state, &rate, &seconds, &trailing) != 3 || state > 3 ||
        !std::isfinite(seconds) || seconds < 0 || seconds > 86400 || !std::isfinite(rate) ||
        rate < 0 || rate > 4)
      return;
    positionAvailable_ = true;
    position_ = static_cast<uint32_t>(seconds * 1000);
    positionAt_ = at;
    playing_ = state == 1;
    rate_ = rate;
    if (!dirty_ && session.track().key[0])
      session.synchronize(position_, playing_, at, rate_);
  }
}
void AmsDecoder::tick(uint32_t now, MediaSession &session) {
  if (!dirty_ || now - changedAt_ < 400)
    return;
  dirty_ = false;
  pending_.key[0] = 0;
  const bool changed = session.replace(pending_, now);
  // Ignore an old position belonging to a previous song. iOS may send the new
  // position immediately before its metadata burst; retain that short window.
  if (positionAvailable_ && (!changed || now - positionAt_ <= 1500))
    session.synchronize(position_, playing_, positionAt_, rate_);
}
} // namespace nova

#include "Diagnostics.h"
#include "../core/Text.h"
#include <Arduino.h>
#include <Preferences.h>
#include <cstring>
#include <ctime>
#include <memory>
#include <new>
namespace nova {
void Diagnostics::begin() {
  Preferences p;
  if (!p.begin("nova-diag", true))
    return;
  auto candidate = std::unique_ptr<Record>(new (std::nothrow) Record);
  if (!candidate) {
    p.end();
    return;
  }
  for (const char *key : {"a", "b"})
    if (p.getBytesLength(key) == sizeof(Record) &&
        p.getBytes(key, candidate.get(), sizeof(Record)) == sizeof(Record) &&
        candidate->magic == history_.magic && candidate->count <= 16 &&
        crc32(candidate.get(), offsetof(Record, crc)) == candidate->crc) {
      bool valid = true;
      for (unsigned i = 0; i < candidate->count; ++i)
        valid = valid && memchr(candidate->entries[i].text, 0, 96);
      if (valid && int32_t(candidate->sequence - history_.sequence) > 0)
        history_ = *candidate;
    }
  p.end();
}
bool Diagnostics::save() {
  Preferences p;
  if (!p.begin("nova-diag", false))
    return false;
  history_.crc = crc32(&history_, offsetof(Record, crc));
  const char *key = history_.sequence & 1 ? "a" : "b";
  auto check = std::unique_ptr<Record>(new (std::nothrow) Record);
  const bool ok = check && p.putBytes(key, &history_, sizeof(history_)) == sizeof(history_) &&
                  p.getBytes(key, check.get(), sizeof(Record)) == sizeof(Record) &&
                  !memcmp(check.get(), &history_, sizeof(history_));
  p.end();
  return ok;
}
void Diagnostics::record(const char *text) {
  if (history_.count == 16) {
    for (unsigned i = 1; i < 16; ++i)
      history_.entries[i - 1] = history_.entries[i];
    --history_.count;
  }
  auto &e = history_.entries[history_.count++];
  e.epoch = time(nullptr);
  e.uptime = millis() / 1000;
  cleanUtf8(e.text, sizeof(e.text), text);
  ++history_.sequence;
  save();
}
bool Diagnostics::clear() {
  history_.count = 0;
  ++history_.sequence;
  return save();
}
void Diagnostics::format(char *out, size_t cap) const {
  if (!cap)
    return;
  out[0] = 0;
  size_t at = 0;
  for (unsigned i = 0; i < history_.count && at + 1 < cap; ++i) {
    const auto &e = history_.entries[i];
    const int n = snprintf(out + at, cap - at, "%lld +%lus %s\n", static_cast<long long>(e.epoch),
                           static_cast<unsigned long>(e.uptime), e.text);
    if (n < 0 || size_t(n) >= cap - at)
      break;
    at += n;
  }
}
} // namespace nova

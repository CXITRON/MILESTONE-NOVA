#include "Lyrics.h"
#include "../core/Text.h"
#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
namespace nova {
std::string_view Lyrics::line(size_t i) const {
  if (i >= count)
    return {};
  return {text.data() + lines[i].start, lines[i].length};
}
bool parseTimestamp(std::string_view text, uint32_t &ms) {
  const auto colon = text.find(':');
  if (colon == text.npos || colon == 0 || colon > 4)
    return false;
  uint32_t minutes = 0;
  for (char c : text.substr(0, colon)) {
    if (c < '0' || c > '9')
      return false;
    minutes = minutes * 10 + c - '0';
  }
  text.remove_prefix(colon + 1);
  if (text.size() < 2 || text[0] < '0' || text[0] > '5' || text[1] < '0' || text[1] > '9')
    return false;
  uint32_t fraction = 0;
  if (text.size() > 2) {
    if (text[2] != '.' || text.size() < 4 || text.size() > 6)
      return false;
    unsigned factor = 100;
    for (char c : text.substr(3)) {
      if (c < '0' || c > '9')
        return false;
      fraction += (c - '0') * factor;
      factor /= 10;
    }
  }
  ms = minutes * 60000 + ((text[0] - '0') * 10 + text[1] - '0') * 1000 + fraction;
  return true;
}
void LrcParser::parse(std::string_view input, Lyrics &out) const {
  out.count = out.used = out.rejected = 0;
  out.synced = out.truncated = false;
  if (input.size() > maxLyricsBytes) {
    input = input.substr(0, maxLyricsBytes);
    out.truncated = true;
  }
  if (input.substr(0, 3) == "\xEF\xBB\xBF")
    input.remove_prefix(3);
  int32_t offset = 0;
  uint16_t order = 0;
  while (!input.empty()) {
    auto e = input.find('\n');
    if (e == input.npos)
      e = input.size();
    auto line = input.substr(0, e);
    input.remove_prefix(e + (e < input.size()));
    if (!line.empty() && line.back() == '\r')
      line.remove_suffix(1);
    if (line.size() > 1024) {
      ++out.rejected;
      continue;
    }
    std::array<uint32_t, 16> times{};
    size_t count = 0;
    bool tag = false, bad = false;
    while (!line.empty() && line.front() == '[') {
      const auto close = line.find(']');
      if (close == line.npos) {
        bad = true;
        break;
      }
      const auto body = line.substr(1, close - 1);
      uint32_t t;
      if (parseTimestamp(body, t)) {
        if (count < times.size())
          times[count++] = t;
        else
          out.truncated = true;
      } else if (body.substr(0, 7) == "offset:") {
        auto value = body.substr(7);
        int32_t n = 0;
        if (!value.empty() && value.front() == '+')
          value.remove_prefix(1);
        auto parsed = std::from_chars(value.data(), value.data() + value.size(), n);
        if (parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() && n >= -600000 &&
            n <= 600000)
          offset = n;
        else
          ++out.rejected;
      } else if (!body.empty() && body.front() >= '0' && body.front() <= '9')
        bad = true;
      tag = true;
      line.remove_prefix(close + 1);
    }
    if (bad) {
      ++out.rejected;
      continue;
    }
    if (!count && (tag || line.empty()))
      continue;
    if (out.count + (count ? count : 1) > maxLyricsLines ||
        out.used + line.size() + 1 > out.text.size()) {
      out.truncated = true;
      continue;
    }
    const auto start = static_cast<uint16_t>(out.used);
    const auto len = static_cast<uint16_t>(
        cleanUtf8(out.text.data() + out.used, out.text.size() - out.used, line));
    out.used += len + 1;
    if (count) {
      out.synced = true;
      for (size_t n = 0; n < count; ++n)
        out.lines[out.count++] = {times[n], start, len, order++};
    } else
      out.lines[out.count++] = {UINT32_MAX, start, len, order++};
  }
  if (out.synced) {
    size_t n = 0;
    for (size_t i = 0; i < out.count; ++i)
      if (out.lines[i].timeMs != UINT32_MAX) {
        out.lines[n] = out.lines[i];
        out.lines[n++].timeMs = static_cast<uint32_t>(
            std::clamp<int64_t>(int64_t(out.lines[i].timeMs) + offset, 0, UINT32_MAX));
      }
    out.count = n;
    std::sort(out.lines.begin(), out.lines.begin() + out.count,
              [](const LyricLine &a, const LyricLine &b) {
                return a.timeMs < b.timeMs || (a.timeMs == b.timeMs && a.order < b.order);
              });
  }
}
int LyricsTimeline::index(const Lyrics &lyrics, uint32_t position) const {
  if (!lyrics.synced || !lyrics.count)
    return -1;
  const auto first = lyrics.lines.begin();
  auto found = std::upper_bound(first, first + lyrics.count, position,
                                [](uint32_t p, const LyricLine &line) { return p < line.timeMs; });
  return found == first ? -1 : static_cast<int>(found - first) - 1;
}
bool LyricsCache::path(char *out, size_t size, const char *key) {
  if (!validTrackKey(key))
    return false;
  const int n = snprintf(out, size, "/lyrics/%s.lrc", key);
  return n > 0 && static_cast<size_t>(n) < size;
}
} // namespace nova

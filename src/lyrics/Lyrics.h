#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
namespace nova {
inline constexpr size_t maxLyricsBytes = 32768, maxLyricsLines = 512;
struct LyricLine {
  uint32_t timeMs;
  uint16_t start, length, order;
};
struct Lyrics {
  std::array<LyricLine, maxLyricsLines> lines{};
  std::array<char, maxLyricsBytes> text{};
  size_t count = 0, used = 0, rejected = 0;
  bool synced = false, truncated = false;
  std::string_view line(size_t i) const;
};
bool parseTimestamp(std::string_view text, uint32_t &ms);
class LrcParser {
public:
  void parse(std::string_view input, Lyrics &out) const;
};
class LyricsTimeline {
public:
  int index(const Lyrics &lyrics, uint32_t positionMs) const;
};
class LyricsProvider {
public:
  virtual ~LyricsProvider() = default;
  virtual bool request(const char *trackKey, uint32_t generation) = 0;
};
class LyricsCache {
public:
  static bool path(char *out, size_t size, const char *trackKey);
};
} // namespace nova

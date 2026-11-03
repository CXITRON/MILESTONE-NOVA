#pragma once
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string_view>
namespace nova {
// Newest memos first. Names are YYYYMMDD-HHMMSS, so newer sorts higher as text.
struct MemoIndex {
  static constexpr unsigned max = 32, nameSize = 16;
  unsigned count = 0, total = 0; // total counts every memo file found, shown or not
  char names[max][nameSize]{};
  void clear() { count = total = 0; }
  void add(const char *name); // Keeps the `max` newest; ignores duplicates.
};
// "/memo/20261103-190512.txt" -> "20261103-190512"; false for anything else.
bool memoName(const char *path, char (&name)[MemoIndex::nameSize]);
void memoPath(const char *name, char *out, size_t size);
// Local time to a name; `bump` seconds are added to step past an existing file.
void memoNameAt(time_t epoch, unsigned bump, char (&name)[MemoIndex::nameSize]);
// "20261103-190512" -> "2026-11-03 19:05" for lists and titles.
void memoLabel(const char *name, char *out, size_t size);
// Line breaks for memo text in Unifont cells (8 px below U+0080, 16 px otherwise).
// Fills up to `max` lines and returns how many there are in total.
struct MemoLine {
  uint16_t start, end; // Byte offsets; `end` excludes a newline.
};
unsigned wrapMemo(std::string_view text, int width, MemoLine *lines, unsigned max);
} // namespace nova

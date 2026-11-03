#include "MemoNames.h"
#include "../core/Text.h"
#include <cctype>
#include <cstdio>
#include <cstring>
namespace nova {
namespace {
bool validName(const char *s) {
  if (strlen(s) != 15 || s[8] != '-')
    return false;
  for (int i = 0; i < 15; ++i)
    if (i != 8 && !isdigit(static_cast<unsigned char>(s[i])))
      return false;
  return true;
}
} // namespace
void MemoIndex::add(const char *name) {
  ++total;
  unsigned at = 0;
  while (at < count && strcmp(names[at], name) > 0)
    ++at;
  if (at < count && !strcmp(names[at], name)) {
    --total;
    return;
  }
  if (at == max)
    return;
  const unsigned last = count < max ? count : max - 1;
  memmove(names[at + 1], names[at], (last - at) * nameSize);
  snprintf(names[at], nameSize, "%s", name);
  if (count < max)
    ++count;
}
bool memoName(const char *path, char (&name)[MemoIndex::nameSize]) {
  if (strncmp(path, "/memo/", 6) || strlen(path) != 25 || strcmp(path + 21, ".txt"))
    return false;
  memcpy(name, path + 6, 15);
  name[15] = 0;
  return validName(name);
}
void memoPath(const char *name, char *out, size_t size) {
  snprintf(out, size, "/memo/%s.txt", name);
}
void memoNameAt(time_t epoch, unsigned bump, char (&name)[MemoIndex::nameSize]) {
  const time_t at = epoch + bump;
  tm local{};
  localtime_r(&at, &local);
  if (local.tm_year + 1900 > 9999 || !strftime(name, sizeof(name), "%Y%m%d-%H%M%S", &local))
    snprintf(name, sizeof(name), "00000000-000000");
}
void memoLabel(const char *n, char *out, size_t size) {
  if (!validName(n)) {
    snprintf(out, size, "%s", n);
    return;
  }
  snprintf(out, size, "%.4s-%.2s-%.2s %.2s:%.2s", n, n + 4, n + 6, n + 9, n + 11);
}
unsigned wrapMemo(std::string_view text, int width, MemoLine *lines, unsigned max) {
  unsigned count = 0;
  size_t pos = 0, start = 0;
  int used = 0;
  const auto emit = [&](size_t end) {
    if (count < max)
      lines[count] = {uint16_t(start), uint16_t(end)};
    ++count;
  };
  while (pos < text.size()) {
    size_t next = pos;
    const uint32_t cp = nextCodepoint(text, next);
    if (cp == '\n') {
      emit(pos);
      start = pos = next;
      used = 0;
      continue;
    }
    const int advance = cp < 0x80 ? 8 : 16;
    if (used + advance > width && pos > start) {
      emit(pos);
      start = pos;
      used = 0;
    }
    used += advance;
    pos = next;
  }
  if (pos > start || !count || text.back() == '\n')
    emit(pos);
  return count;
}
} // namespace nova

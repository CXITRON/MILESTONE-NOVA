#include "Language.h"
#include <atomic>
#include <cstring>
namespace nova {
namespace {
std::atomic<uint8_t> current{uint8_t(Language::Korean)};
const siron::Phrase *find(const char *korean) {
  unsigned lo = 0, hi = siron::phraseCount;
  while (lo < hi) {
    const unsigned mid = (lo + hi) / 2;
    const int order = strcmp(siron::phrases[mid].korean, korean);
    if (!order)
      return &siron::phrases[mid];
    if (order < 0)
      lo = mid + 1;
    else
      hi = mid;
  }
  return nullptr;
}
struct Capture {
  const char *at;
  size_t length;
};
constexpr unsigned maxCaptures = 4;
// Matches `text` against a printf pattern with %s/%d; each specifier takes at least one byte and
// %d only digits and a leading minus. Backtracks so "v%s (현재 v%s)" finds the right split.
bool match(const char *pattern, const char *text, Capture *captures, unsigned count) {
  if (!*pattern)
    return !*text;
  if (pattern[0] == '%' && (pattern[1] == 's' || pattern[1] == 'd')) {
    if (count >= maxCaptures)
      return false;
    const bool digits = pattern[1] == 'd';
    for (size_t n = 1; text[n - 1]; ++n) {
      const char c = text[n - 1];
      if (digits && !(c >= '0' && c <= '9') && !(n == 1 && c == '-'))
        return false;
      captures[count] = {text, n};
      if (match(pattern + 2, text + n, captures, count + 1))
        return true;
    }
    return false;
  }
  return *pattern == *text && match(pattern + 1, text + 1, captures, count);
}
} // namespace
void setLanguage(Language language) { current.store(uint8_t(language)); }
Language language() { return Language(current.load()); }
const char *tr(const char *korean) {
  if (!korean || language() != Language::Siron)
    return korean;
  const auto *phrase = find(korean);
  return phrase ? phrase->siron : korean;
}
bool trFormatted(const char *text, char *out, size_t capacity) {
  if (!text || !out || !capacity || language() != Language::Siron)
    return false;
  const siron::Phrase *phrase = find(text);
  Capture captures[maxCaptures]{};
  for (unsigned i = 0; !phrase && i < siron::phraseCount; ++i)
    if (strchr(siron::phrases[i].korean, '%') && match(siron::phrases[i].korean, text, captures, 0))
      phrase = &siron::phrases[i];
  if (!phrase)
    return false;
  size_t at = 0;
  unsigned next = 0;
  for (const char *p = phrase->siron; *p; ++p) {
    const char *piece = p;
    size_t length = 1;
    if (p[0] == '%' && (p[1] == 's' || p[1] == 'd') && next < maxCaptures && captures[next].at) {
      piece = captures[next].at;
      length = captures[next++].length;
      ++p;
    }
    if (at + length >= capacity)
      return false;
    memcpy(out + at, piece, length);
    at += length;
  }
  out[at] = 0;
  return true;
}
} // namespace nova

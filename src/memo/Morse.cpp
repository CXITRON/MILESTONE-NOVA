#include "Morse.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace nova::morse {
void Keyer::reset() {
  clear();
  down_ = false;
  recent_ = next_ = 0;
  split_ = startSplit;
}
void Keyer::clear() {
  length_ = 0;
  code_[0] = 0;
  overflow_ = false;
}
void Keyer::down(uint32_t at) {
  down_ = true;
  downAt_ = at;
}
void Keyer::up(uint32_t at) {
  if (!down_)
    return;
  down_ = false;
  upAt_ = at;
  const uint32_t held = at - downAt_;
  durations_[next_] = held;
  next_ = (next_ + 1) % history;
  recent_ = std::min<uint8_t>(recent_ + 1, history);
  uint32_t shortest = UINT32_MAX, longest = 0;
  for (uint8_t i = 0; i < recent_; ++i) {
    shortest = std::min(shortest, durations_[i]);
    longest = std::max(longest, durations_[i]);
  }
  if (shortest && longest >= 2 * shortest)
    split_ = std::clamp<uint32_t>(uint32_t(std::sqrt(double(shortest) * longest)), minSplit,
                                  maxSplit);
  if (length_ == maxCode) {
    overflow_ = true;
    return;
  }
  code_[length_++] = held < split_ ? '.' : '-';
  code_[length_] = 0;
}
bool Keyer::letterDue(uint32_t now) const {
  return !down_ && length_ && now - upAt_ >= 2 * split_;
}
namespace {
struct Latin {
  const char *code, *text;
};
constexpr Latin latinTable[]{
    {".-", "A"},     {"-...", "B"},   {"-.-.", "C"},   {"-..", "D"},     {".", "E"},
    {"..-.", "F"},   {"--.", "G"},    {"....", "H"},   {"..", "I"},      {".---", "J"},
    {"-.-", "K"},    {".-..", "L"},   {"--", "M"},     {"-.", "N"},      {"---", "O"},
    {".--.", "P"},   {"--.-", "Q"},   {".-.", "R"},    {"...", "S"},     {"-", "T"},
    {"..-", "U"},    {"...-", "V"},   {".--", "W"},    {"-..-", "X"},    {"-.--", "Y"},
    {"--..", "Z"},   {"-----", "0"},  {".----", "1"},  {"..---", "2"},   {"...--", "3"},
    {"....-", "4"},  {".....", "5"},  {"-....", "6"},  {"--...", "7"},   {"---..", "8"},
    {"----.", "9"},  {".-.-.-", "."}, {"--..--", ","}, {"..--..", "?"},  {".----.", "'"},
    {"-.-.--", "!"}, {"-..-.", "/"},  {"-.--.", "("},  {"-.--.-", ")"},  {".-...", "&"},
    {"---...", ":"}, {"-.-.-.", ";"}, {"-...-", "="},  {".-.-.", "+"},   {"-....-", "-"},
    {"..--.-", "_"}, {".-..-.", "\""}, {".--.-.", "@"}};
struct Jamo {
  const char *code;
  uint32_t jamo;
};
// Korean Morse (consonants ㄱ..ㅎ, vowels ㅏ..ㅣ, ㅐ, ㅔ).
constexpr Jamo jamoTable[]{
    {".-..", 0x3131}, {"..-.", 0x3134}, {"-...", 0x3137}, {"...-", 0x3139}, {"--", 0x3141},
    {".--", 0x3142},  {"--.", 0x3145},  {"-.-", 0x3147},  {".--.", 0x3148}, {"-.-.", 0x314A},
    {"-..-", 0x314B}, {"--..", 0x314C}, {"---", 0x314D},  {".---", 0x314E}, {".", 0x314F},
    {"..", 0x3151},   {"-", 0x3153},    {"...", 0x3155},  {".-", 0x3157},   {"-.", 0x315B},
    {"....", 0x315C}, {".-.", 0x3160},  {"-..", 0x3161},  {"..-", 0x3163},  {"--.-", 0x3150},
    {"-.--", 0x3154}};
// Compatibility jamo order of the 19 initials and 27 finals (index + 1) used by U+AC00.
constexpr uint32_t initials[]{0x3131, 0x3132, 0x3134, 0x3137, 0x3138, 0x3139, 0x3141,
                              0x3142, 0x3143, 0x3145, 0x3146, 0x3147, 0x3148, 0x3149,
                              0x314A, 0x314B, 0x314C, 0x314D, 0x314E};
constexpr uint32_t finals[]{0x3131, 0x3132, 0x3133, 0x3134, 0x3135, 0x3136, 0x3137,
                            0x3139, 0x313A, 0x313B, 0x313C, 0x313D, 0x313E, 0x313F,
                            0x3140, 0x3141, 0x3142, 0x3144, 0x3145, 0x3146, 0x3147,
                            0x3148, 0x314A, 0x314B, 0x314C, 0x314D, 0x314E};
struct Pair {
  uint32_t first, second, joined;
};
constexpr Pair doubled[]{{0x3131, 0x3131, 0x3132}, {0x3137, 0x3137, 0x3138},
                         {0x3142, 0x3142, 0x3143}, {0x3145, 0x3145, 0x3146},
                         {0x3148, 0x3148, 0x3149}};
constexpr Pair vowels[]{{0x3157, 0x314F, 0x3158}, {0x3157, 0x3150, 0x3159},
                        {0x3157, 0x3163, 0x315A}, {0x315C, 0x3153, 0x315D},
                        {0x315C, 0x3154, 0x315E}, {0x315C, 0x3163, 0x315F},
                        {0x3161, 0x3163, 0x3162}, {0x3151, 0x3163, 0x3152},
                        {0x3155, 0x3163, 0x3156}};
// Final clusters; ㄱㄱ and ㅅㅅ give ㄲ/ㅆ finals (밖, 있) and move on as one consonant.
constexpr Pair clusters[]{{0x3131, 0x3145, 0x3133}, {0x3134, 0x3148, 0x3135},
                          {0x3134, 0x314E, 0x3136}, {0x3139, 0x3131, 0x313A},
                          {0x3139, 0x3141, 0x313B}, {0x3139, 0x3142, 0x313C},
                          {0x3139, 0x3145, 0x313D}, {0x3139, 0x314C, 0x313E},
                          {0x3139, 0x314D, 0x313F}, {0x3139, 0x314E, 0x3140},
                          {0x3142, 0x3145, 0x3144}, {0x3131, 0x3131, 0x3132},
                          {0x3145, 0x3145, 0x3146}};
template <size_t N> uint32_t join(const Pair (&table)[N], uint32_t a, uint32_t b) {
  for (const auto &p : table)
    if (p.first == a && p.second == b)
      return p.joined;
  return 0;
}
template <size_t N> int indexOf(const uint32_t (&table)[N], uint32_t value) {
  for (size_t i = 0; i < N; ++i)
    if (table[i] == value)
      return int(i);
  return -1;
}
bool vowel(uint32_t j) { return j >= 0x314F && j <= 0x3163; }
bool consonant(uint32_t j) { return j >= 0x3131 && j <= 0x314E; }
// Reads as much of `seq` as one syllable holds. Returns the jamo consumed; `out` is the
// syllable or lone jamo, `finalJamo` how many trailing jamo formed the final consonant.
size_t compose(const uint32_t *seq, size_t n, uint32_t &out, size_t &finalJamo) {
  out = 0;
  finalJamo = 0;
  size_t i = 0;
  uint32_t first = 0, middle = 0;
  if (i < n && consonant(seq[i])) {
    first = seq[i++];
    if (i < n)
      if (const uint32_t d = join(doubled, first, seq[i])) {
        first = d;
        ++i;
      }
  }
  if (i < n && vowel(seq[i])) {
    middle = seq[i++];
    if (i < n)
      if (const uint32_t v = join(vowels, middle, seq[i])) {
        middle = v;
        ++i;
      }
  }
  int last = -1;
  if (first && middle && i < n && indexOf(finals, seq[i]) >= 0) {
    uint32_t f = seq[i++];
    finalJamo = 1;
    if (i < n && consonant(seq[i]))
      if (const uint32_t c = join(clusters, f, seq[i])) {
        f = c;
        ++i;
        finalJamo = 2;
      }
    last = indexOf(finals, f);
  }
  if (first && middle)
    out = 0xAC00 + (indexOf(initials, first) * 21 + (middle - 0x314F)) * 28 + (last + 1);
  else
    out = first ? first : middle;
  return i;
}
} // namespace
const char *latin(const char *code) {
  for (const auto &l : latinTable)
    if (!strcmp(l.code, code))
      return l.text;
  return nullptr;
}
uint32_t jamo(const char *code) {
  for (const auto &j : jamoTable)
    if (!strcmp(j.code, code))
      return j.jamo;
  return 0;
}
uint32_t Composer::current() const {
  uint32_t out;
  size_t finalJamo;
  compose(jamo_, length_, out, finalJamo);
  return out;
}
uint32_t Composer::flush() {
  const uint32_t out = current();
  clear();
  return out;
}
uint32_t Composer::feed(uint32_t j) {
  if (sealed_ || length_ == maxJamo) {
    const uint32_t done = flush();
    jamo_[length_++] = j;
    return done;
  }
  jamo_[length_] = j;
  uint32_t out;
  size_t finalJamo;
  if (compose(jamo_, length_ + 1, out, finalJamo) == size_t(length_) + 1) {
    ++length_;
    return 0;
  }
  // A vowel after a final consonant takes it: the last cluster member, or a whole ㄲ/ㅆ.
  compose(jamo_, length_, out, finalJamo);
  if (vowel(j) && finalJamo) {
    size_t moved = 1;
    if (finalJamo == 2 && jamo_[length_ - 1] == jamo_[length_ - 2])
      moved = 2;
    uint32_t carry[2]{};
    for (size_t k = 0; k < moved; ++k)
      carry[k] = jamo_[length_ - moved + k];
    length_ -= moved;
    const uint32_t done = current();
    length_ = 0;
    for (size_t k = 0; k < moved; ++k)
      jamo_[length_++] = carry[k];
    jamo_[length_++] = j;
    return done;
  }
  const uint32_t done = flush();
  jamo_[length_++] = j;
  return done;
}
bool Composer::erase() {
  if (!length_)
    return false;
  --length_;
  sealed_ = false;
  return true;
}
size_t encodeUtf8(uint32_t cp, char (&out)[5]) {
  size_t n = 0;
  if (cp < 0x80)
    out[n++] = char(cp);
  else if (cp < 0x800) {
    out[n++] = char(0xC0 | cp >> 6);
    out[n++] = char(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out[n++] = char(0xE0 | cp >> 12);
    out[n++] = char(0x80 | (cp >> 6 & 0x3F));
    out[n++] = char(0x80 | (cp & 0x3F));
  } else {
    out[n++] = char(0xF0 | cp >> 18);
    out[n++] = char(0x80 | (cp >> 12 & 0x3F));
    out[n++] = char(0x80 | (cp >> 6 & 0x3F));
    out[n++] = char(0x80 | (cp & 0x3F));
  }
  out[n] = 0;
  return n;
}
void Editor::clear() {
  length_ = 0;
  text_[0] = 0;
  composer_.clear();
}
// The open syllable is always shown, so it counts against the capacity before it closes.
bool Editor::room(size_t extra) const { return length_ + 3 + extra <= capacity; }
bool Editor::append(const char *utf8) {
  const size_t n = strlen(utf8);
  if (length_ + n > capacity)
    return false;
  memcpy(text_ + length_, utf8, n + 1);
  length_ += n;
  return true;
}
bool Editor::append(uint32_t cp) {
  if (!cp)
    return true;
  char out[5];
  encodeUtf8(cp, out);
  return append(out);
}
void Editor::toggleAlphabet() {
  append(composer_.flush());
  hangul_ = !hangul_;
}
Editor::Result Editor::letter(const char *code) {
  // Korean Morse has no digits or punctuation; their five- and six-symbol codes do not collide
  // with jamo, so they work in both alphabets.
  if (hangul_)
    if (const uint32_t j = jamo(code)) {
      if (!room(3))
        return Result::Full;
      append(composer_.feed(j));
      return Result::Added;
    }
  const char *text = latin(code);
  if (!text || (hangul_ && strlen(code) < 5))
    return Result::Unknown;
  if (!room(strlen(text)))
    return Result::Full;
  append(composer_.flush());
  append(text);
  return Result::Added;
}
Editor::Result Editor::space() {
  if (!room(1))
    return Result::Full;
  append(composer_.flush());
  append(" ");
  return Result::Added;
}
void Editor::erase() {
  if (composer_.erase())
    return;
  // Remove one whole UTF-8 codepoint.
  while (length_ && (uint8_t(text_[length_ - 1]) & 0xC0) == 0x80)
    --length_;
  if (length_)
    --length_;
  text_[length_] = 0;
}
size_t Editor::preview(char *out, size_t size) const {
  if (!size)
    return 0;
  char open[5] = "";
  if (const uint32_t cp = composer_.current())
    encodeUtf8(cp, open);
  const size_t a = std::min(length_, size - 1);
  memcpy(out, text_, a);
  const size_t b = std::min(strlen(open), size - 1 - a);
  memcpy(out + a, open, b);
  out[a + b] = 0;
  return a + b;
}
const char *Editor::finish() {
  append(composer_.flush());
  return text_;
}
} // namespace nova::morse

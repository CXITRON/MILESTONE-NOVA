#include "../memo/Morse.h"
#include "Ui.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string_view>
namespace nova {
namespace {
constexpr int lineStep = 18;
// Keyed text has no newlines, so 1 KiB wraps to about 40 lines; files edited elsewhere can
// have more, and only their first 128 lines can be scrolled to.
MemoLine wrapped[128];
void lines(Canvas &c, std::string_view text, int y, unsigned first, unsigned shown,
           const MemoLine *l, unsigned count, uint16_t color) {
  for (unsigned i = first; i < std::min(count, first + shown); ++i)
    c.text(16, y + int(i - first) * lineStep, memoTextWidth, 16,
           text.substr(l[i].start, l[i].end - l[i].start), color);
}
// Dots as squares, dashes as bars, centred: the symbol shapes read faster than "." and "-".
void symbols(Canvas &c, const char *code, int y) {
  int width = 0;
  for (const char *p = code; *p; ++p)
    width += (*p == '.' ? 8 : 24) + 8;
  int x = (board::width - width + 8) / 2;
  for (const char *p = code; *p; ++p) {
    const int w = *p == '.' ? 8 : 24;
    c.rect(x, y, w, 8, color::white);
    x += w + 8;
  }
}
void list(Canvas &c, const MemoPanel &m) {
  if (m.index && m.index->total) {
    char count[24];
    snprintf(count, sizeof(count), tr("%u개"), m.index->total);
    c.text(112, 39, 112, 20, count, c.muted());
  }
  if (m.loading) {
    c.text(16, 140, 208, 20, tr("불러오는 중"), c.muted(), 1, true);
    return;
  }
  const unsigned items = 1 + (m.index ? m.index->count : 0);
  const unsigned start = m.item > 6 ? m.item - 6 : 0;
  for (unsigned i = start; i < std::min(start + 7, items); ++i) {
    const int y = 76 + int(i - start) * 28;
    if (i == m.item)
      c.rect(10, y - 3, 220, 27, color::panel);
    char label[32];
    if (i)
      memoLabel(m.index->names[i - 1], label, sizeof(label));
    else
      snprintf(label, sizeof(label), "+ %s", tr("새 메모"));
    c.text(18, y, 204, 22, label, i == m.item ? c.accent() : i ? color::white : c.muted());
  }
  if (items == 1)
    c.text(16, 130, 208, 60, tr("아직 메모가 없습니다"), c.muted(), 1, true);
}
void edit(Canvas &c, const View &v, const MemoPanel &m) {
  c.text(176, 39, 48, 20, m.hangul ? tr("한글") : "ABC", color::warning);
  // The newest lines stay visible; older ones scroll off the top.
  constexpr unsigned shown = 8;
  const MemoLine *l = wrapped;
  const std::string_view text = m.text;
  const unsigned count = std::min<unsigned>(
      wrapMemo(text, memoTextWidth, wrapped, std::size(wrapped)), std::size(wrapped));
  const unsigned first = count > shown ? count - shown : 0;
  c.rect(10, 64, 220, 8 + shown * lineStep, color::panel);
  lines(c, text, 68, first, shown, l, count, color::white);
  if (v.now / 500 % 2 || m.pressed) {
    // Cursor after the last character.
    const auto &last = l[count - 1];
    const int x = 16 + c.textWidth(text.substr(last.start, last.end - last.start));
    c.rect(std::min(x, 222), 68 + int(count - 1 - first) * lineStep, 2, 16, c.accent());
  }
  if (m.code[0])
    symbols(c, m.code, 222);
  if (m.pressed && m.split) {
    // Held time against the dot/dash split: accent while it is still a dot.
    const uint32_t full = 2 * m.split;
    const int w = int(std::min<uint32_t>(m.held, full) * 208 / full);
    c.rect(16, 236, 208, 4, color::panel);
    c.rect(16, 236, w, 4, m.held < m.split ? c.accent() : color::warning);
    c.rect(16 + 104, 234, 1, 8, c.muted());
  }
  // What the code so far would give, once the letter gap passes.
  if (m.code[0]) {
    char guess[5] = "?";
    const uint32_t j = m.hangul ? morse::jamo(m.code) : 0;
    if (j)
      morse::encodeUtf8(j, guess);
    else if (!m.hangul || strlen(m.code) >= 5)
      if (const char *t = morse::latin(m.code))
        snprintf(guess, sizeof(guess), "%s", t);
    c.text(16, 246, 208, 18, guess, c.accent(), 1, true);
  }
  c.text(16, 270, 208, 18, tr("BACK 저장 / MENU 한/영"), c.muted(), 1, true);
}
void opened(Canvas &c, const MemoPanel &m) {
  char label[32];
  memoLabel(m.name, label, sizeof(label));
  c.text(16, 64, 208, 18, label, c.muted());
  if (m.loading) {
    c.text(16, 140, 208, 20, tr("불러오는 중"), c.muted(), 1, true);
    return;
  }
  const MemoLine *l = wrapped;
  const std::string_view text = m.text;
  const unsigned count = std::min<unsigned>(
      wrapMemo(text, memoTextWidth, wrapped, std::size(wrapped)), std::size(wrapped));
  const unsigned first = std::min(m.scroll, count > memoViewLines ? count - memoViewLines : 0);
  lines(c, text, 88, first, memoViewLines, l, count, color::white);
  if (count > memoViewLines) {
    // Scroll position on the right edge.
    const int track = int(memoViewLines) * lineStep;
    const int h = std::max(12, track * int(memoViewLines) / int(count));
    const int y = 88 + (track - h) * int(first) / int(count - memoViewLines);
    c.rect(228, 88, 2, track, color::panel);
    c.rect(228, y, 2, h, c.accent());
  }
  if (m.confirm)
    c.text(16, 270, 208, 18, tr("한 번 더 길게 OK: 삭제"), color::danger, 1, true);
}
} // namespace
void memoScreen(Canvas &c, const View &v) {
  if (!v.memo)
    return;
  if (v.screen == Screen::MemoList)
    list(c, *v.memo);
  else if (v.screen == Screen::MemoEdit)
    edit(c, v, *v.memo);
  else
    opened(c, *v.memo);
}
} // namespace nova

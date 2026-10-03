#include "Renderer.h"
namespace nova {
void LyricsRenderer::draw(Canvas &c, const Lyrics &l, int current, bool positionKnown) const {
  if (!l.synced) {
    c.text(16, 76, 208, 20, "UNSYNCED LYRICS", c.muted());
    size_t count = 0;
    for (size_t n = 0; n < l.count && count < 4; ++n, ++count)
      c.text(16, 112 + count * 36, 208, 36, l.line(n), color::white);
    return;
  }
  if (!positionKnown) {
    c.text(16, 134, 208, 60, "재생 위치를 기다리는 중", c.muted(), 1, true);
    return;
  }
  if (current < 0) {
    c.text(16, 118, 208, 60, "가사가 곧 시작됩니다", c.muted(), 1, true);
    c.text(16, 206, 208, 54, l.line(0), color::white, 1, true);
    return;
  }
  if (current > 0)
    c.text(20, 72, 200, 36, l.line(current - 1), c.muted(), 1, true);
  const auto active = l.line(current);
  c.rect(8, 113, 3, 100, c.accent());
  if (active.empty())
    c.text(20, 145, 200, 48, "INSTRUMENTAL", c.accent(), 1, true);
  else
    c.text(20, 116, 200, 104, active, color::white, 2, true);
  if (size_t(current + 1) < l.count)
    c.text(20, 224, 200, 36, l.line(current + 1), c.muted(), 1, true);
}
void LyricsRenderer::drawCompact(Canvas &c, const Lyrics *l, int current, bool positionKnown,
                                 int top) const {
  if (!l || !l->count) {
    c.text(16, top + 44, 208, 20, "No lyrics", c.muted(), 1, true);
    return;
  }
  if (!l->synced) {
    c.text(16, top, 208, 20, "UNSYNCED", c.muted(), 1, true);
    for (size_t n = 0; n < l->count && n < 3; ++n)
      c.text(16, top + 26 + int(n) * 32, 208, 30, l->line(n), color::white, 1, true);
    return;
  }
  if (!positionKnown) {
    c.text(16, top + 44, 208, 40, "재생 위치를 기다리는 중", c.muted(), 1, true);
    return;
  }
  if (current < 0) {
    c.text(16, top + 8, 208, 20, "가사가 곧 시작됩니다", c.muted(), 1, true);
    c.text(20, top + 40, 200, 44, l->line(0), color::white, 1, true);
    return;
  }
  if (current > 0)
    c.text(20, top, 200, 20, l->line(current - 1), c.muted(), 1, true);
  c.rect(8, top + 26, 3, 50, c.accent());
  const auto active = l->line(current);
  if (active.empty())
    c.text(20, top + 40, 200, 22, "INSTRUMENTAL", c.accent(), 1, true);
  else
    c.text(20, top + 28, 200, 46, active, color::white, 1, true);
  if (size_t(current + 1) < l->count)
    c.text(20, top + 84, 200, 40, l->line(current + 1), c.muted(), 1, true);
}
} // namespace nova

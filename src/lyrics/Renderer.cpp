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
} // namespace nova

#include "../lyrics/Renderer.h"
#include "Ui.h"
#include <cstdio>
namespace nova {
void nowScreen(Canvas &c, const View &v) {
  const auto &track = v.media->track();
  const auto &s = *v.settings;
  char elapsed[24], total[24], line[64];
  if (v.media->positionKnown())
    formatTime(elapsed, sizeof(elapsed), v.media->position(v.now));
  else
    snprintf(elapsed, sizeof(elapsed), "--:--");
  if (track.durationMs)
    formatTime(total, sizeof(total), track.durationMs);
  else
    snprintf(total, sizeof(total), "--:--");
  snprintf(line, sizeof(line), "%s / %s", elapsed, total);
  c.text(72, 39, 140, 20, line, s.mutedColor, 1, true);
  const char *title = track.title[0] ? track.title : "미디어 수신 대기",
             *artist = track.artist[0] ? track.artist : "Unknown artist",
             *album = track.album[0] ? track.album : "Unknown album";
  const auto label = [&](int y, const char *text, uint16_t ink) {
    c.marquee(16, y, 208, text, ink, v.now, s.scroll ? s.scrollSpeed : 0, !s.alignLeft);
  };
  const auto cover = [&](int x, int y, unsigned side) {
    if (v.artwork)
      c.scaledImage(x, y, side, v.artwork, board::artSide);
    else {
      c.rect(x, y, side, side, color::panel);
      c.text(x + 4, y + int(side) / 2 - 8, side - 8, 20, "NO ART", s.mutedColor, 1, true);
    }
  };
  if (s.nowLayout == 4 && s.lyricsView && v.lyrics && v.lyrics->count) {
    LyricsTimeline timeline;
    LyricsRenderer{}.draw(c, *v.lyrics, timeline.index(*v.lyrics, v.media->position(v.now)),
                          v.media->positionKnown());
    label(264, title, s.accentColor);
  } else if (s.nowLayout == 1) {
    cover(16, 78, 88);
    c.text(116, 86, 108, 54, artist, s.mutedColor);
    c.text(116, 146, 108, 34, v.media->playing() ? "PLAYING" : "PAUSED", s.accentColor);
    label(194, title, s.timeColor);
    label(232, album, s.mutedColor);
  } else if (s.nowLayout == 2) {
    c.text(18, 86, 204, 92, title, s.timeColor, 2, !s.alignLeft);
    label(198, artist, s.accentColor);
    label(237, album, s.mutedColor);
  } else {
    // Large cover: native 200x200 between the header row and one scrolling caption.
    const int side = board::artSide, left = (board::width - side) / 2, top = 60;
    cover(left, top, side);
    char caption[sizeof(track.title) + sizeof(track.album) + 8];
    snprintf(caption, sizeof(caption), "%s / %s", title, s.nowLayout == 3 ? album : artist);
    label(top + side + 5, caption, s.timeColor);
    if (s.nowLayout == 4 && (!v.lyrics || !v.lyrics->count)) {
      c.rect(left + 20, top + side - 30, side - 40, 24, color::panel);
      c.text(left + 24, top + side - 28, side - 48, 20, "No local lyrics", s.mutedColor, 1, true);
    }
  }
  progressBar(c, 289, v.media->position(v.now), track.durationMs);
  if (!v.media->playing())
    c.rect(218, 40, 3, 12, color::warning);
}
} // namespace nova

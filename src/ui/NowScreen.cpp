#include "../lyrics/Renderer.h"
#include "Ui.h"
#include <cstdio>
namespace nova {
namespace {
// Displayed cover side; the cached source stays board::artSide and is area-averaged down.
constexpr int coverSide = 172;
} // namespace
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
  } else if (s.nowLayout == 5) {
    // Combined: small cover with title/artist above a three-line lyric panel.
    cover(16, 62, 88);
    const auto side = [&](int y, const char *text, uint16_t ink) {
      c.marquee(114, y, 110, text, ink, v.now, s.scroll ? s.scrollSpeed : 0, false);
    };
    side(66, title, s.timeColor);
    side(94, artist, s.mutedColor);
    side(122, album, s.mutedColor);
    LyricsTimeline timeline;
    const int current =
        v.lyrics && v.lyrics->count ? timeline.index(*v.lyrics, v.media->position(v.now)) : -1;
    LyricsRenderer{}.drawCompact(c, s.lyricsView ? v.lyrics : nullptr, current,
                                 v.media->positionKnown(), 160);
  } else {
    // Large cover sized so title and artist (or album) each keep their own line.
    const int side = coverSide, left = (board::width - side) / 2, top = 60;
    cover(left, top, side);
    label(top + side + 4, title, s.timeColor);
    label(top + side + 28, s.nowLayout == 3 ? album : artist, s.mutedColor);
    if (s.nowLayout == 4 && (!v.lyrics || !v.lyrics->count)) {
      c.rect(left + 16, top + side - 30, side - 32, 24, color::panel);
      c.text(left + 20, top + side - 28, side - 40, 20, "No lyrics", s.mutedColor, 1, true);
    }
  }
  progressBar(c, 289, v.media->position(v.now), track.durationMs);
  if (!v.media->playing())
    c.rect(218, 40, 3, 12, color::warning);
}
} // namespace nova

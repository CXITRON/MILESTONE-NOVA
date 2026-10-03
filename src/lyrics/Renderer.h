#pragma once
#include "../display/Canvas.h"
#include "Lyrics.h"
namespace nova {
class LyricsRenderer {
public:
  void draw(Canvas &canvas, const Lyrics &lyrics, int current, bool positionKnown) const;
  // Three-line panel (previous / current / next) for layouts that also show artwork.
  void drawCompact(Canvas &canvas, const Lyrics *lyrics, int current, bool positionKnown,
                   int top) const;
};
} // namespace nova

#pragma once
#include "../display/Canvas.h"
#include "Lyrics.h"
namespace nova {
class LyricsRenderer {
public:
  void draw(Canvas &canvas, const Lyrics &lyrics, int current, bool positionKnown) const;
};
} // namespace nova

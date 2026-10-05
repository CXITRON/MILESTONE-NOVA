#include "Ui.h"
#include <cstdio>
namespace nova {
void settingsScreen(Canvas &c, const View &v) {
  static constexpr const char *names[]{"LCD brightness", "RGB brightness", "Focus minutes",
                                       "Lyrics view",    "Wi-Fi retry",    "Signed OTA",
                                       "Power OFF"};
  for (unsigned i = 0; i < 7; ++i) {
    const int y = 70 + i * 24;
    if (i == v.setting)
      c.rect(10, y - 2, 220, 23, color::panel);
    c.text(16, y, 144, 20, names[i], i == v.setting ? c.accent() : c.muted());
    char value[32]{};
    switch (i) {
    case 0:
      snprintf(value, sizeof(value), "%u", v.settings->lcdBrightness);
      break;
    case 1:
      snprintf(value, sizeof(value), "%u", v.settings->rgbBrightness);
      break;
    case 2:
      snprintf(value, sizeof(value), "%lu",
               static_cast<unsigned long>(v.settings->focusSeconds / 60));
      break;
    case 3:
      snprintf(value, sizeof(value), "%s", v.settings->lyricsView ? "ON" : "OFF");
      break;
    case 4:
    case 5:
    case 6:
      snprintf(value, sizeof(value), "OK");
      break;
    }
    c.text(168, y, 56, 20, value,
           i == v.setting && v.settingsEditing ? color::warning : color::white);
  }
  c.text(16, 265, 208, 28, v.updateStatus, c.muted());
}
} // namespace nova

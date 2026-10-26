#include "Ui.h"
#include <cstdio>
namespace nova {
void settingsScreen(Canvas &c, const View &v) {
  static constexpr const char *names[]{"LCD brightness", "RGB brightness", "Focus minutes",
                                       "Lyrics view",    "Language",       "Wi-Fi retry",
                                       "Signed OTA",     "Power OFF"};
  for (unsigned i = 0; i < settingItems; ++i) {
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
      // The language name stays in its own script: 한국어, or "siron" in Siron letters.
      snprintf(value, sizeof(value), "%s", language() == Language::Siron ? tr("시론") : "한국어");
      break;
    case 5:
    case 6:
    case 7:
      snprintf(value, sizeof(value), "OK");
      break;
    }
    c.text(168, y, 56, 20, value,
           i == v.setting && v.settingsEditing ? color::warning : color::white);
  }
  char status[160];
  c.text(16, 265, 208, 28, shown(v.updateStatus, status, sizeof(status)), c.muted());
}
} // namespace nova

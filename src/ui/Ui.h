#pragma once
#include "../core/Logic.h"
#include "../display/Canvas.h"
#include "../lyrics/Lyrics.h"
#include "../media/Session.h"
#include "../settings/Values.h"
#include "Navigation.h"
#include <ctime>
namespace nova {
// Microseconds spent in each part of Ui::render, filled only when the app supplies a clock.
struct RenderTimes {
  uint32_t clear = 0, chrome = 0, screen = 0, overlay = 0, post = 0;
};
struct View {
  int64_t (*clock)() = nullptr;  // Optional microsecond clock; null disables timing.
  RenderTimes *times = nullptr;
  Screen screen = Screen::Clock;
  Profile mode = Profile::Core;
  unsigned menu = 0, systemPage = 0, recoveryItem = 0, updateProgress = 0;
  uint32_t stack = 0;
  float chipTemperature = 0;
  int thermalState = 0;
  bool canPlay = false, canNext = false, canPrev = false, sync = false, syncStale = false,
       updateReady = false;
  const char *apPassword = "", *updateStatus = "";
  const Settings *settings = nullptr;
  const MediaSession *media = nullptr;
  const Lyrics *lyrics = nullptr;
  const FocusTimer *timer = nullptr;
  const uint16_t *artwork = nullptr;
  const uint16_t *mediaPixels = nullptr;
  uint32_t now = 0, uptime = 0, freeHeap = 0, minHeap = 0, freePsram = 0, loopMaxUs = 0;
  time_t epoch = 0;
  bool environmentValid = false, batteryValid = false, wifi = false, ble = false, sd = false,
       rtc = false;
  bool mediaPlaying = false, sleeping = false, settingsEditing = false, warning = false;
  float temperature = 0, humidity = 0, volts = 0, percent = 0;
  uint32_t mediaPosition = 0, mediaDuration = 0;
  unsigned setting = 0;
  const char *bleStatus = "Off";
  const char *wifiStatus = "Off";
  const char *ip = "--";
  const char *mediaName = "No media";
  const char *notice = "";
};
class Ui {
public:
  void render(Canvas &canvas, const View &view);

private:
  void chrome(Canvas &canvas, const View &view);
};
void coreScreen(Canvas &canvas, const View &view);
void nowScreen(Canvas &canvas, const View &view);
void settingsScreen(Canvas &canvas, const View &view);
void mediaScreen(Canvas &canvas, const View &view);
void progressBar(Canvas &canvas, int y, uint32_t position, uint32_t duration);
void formatTime(char *out, size_t size, uint32_t ms);
} // namespace nova

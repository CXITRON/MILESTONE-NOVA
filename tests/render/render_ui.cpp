#include "ui/Ui.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
using namespace nova;
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  setenv("TZ", "KST-9", 1);
  tzset();
  Canvas c;
  if (!c.begin())
    return 1;
  Settings settings;
  Track track;
  strcpy(track.title, "한글과 English 제목");
  strcpy(track.artist, "NOVA TEST");
  track.durationMs = 231000;
  MediaSession media;
  media.replace(track, 0);
  media.synchronize(134000, true, 5000);
  Lyrics lyrics;
  LrcParser{}.parse(
      "[02:10]이전 줄 / Previous\n[02:13]지금의 순간을 기억해요\n[02:18]다음 줄 / Next", lyrics);
  FocusTimer timer;
  timer.reset(1500);
  timer.toggle(0);
  Ui ui;
  View v;
  v.settings = &settings;
  v.media = &media;
  v.lyrics = &lyrics;
  v.timer = &timer;
  v.now = 5000;
  v.epoch = 1789608900;
  v.environmentValid = true;
  v.temperature = 23.4f;
  v.humidity = 47.2f;
  v.batteryValid = true;
  v.percent = 78;
  v.volts = 3.95;
  v.wifi = v.ble = v.sd = v.rtc = true;
  v.wifiStatus = "Connected";
  v.bleStatus = "AMS ready";
  v.ip = "192.168.1.42";
  v.freeHeap = 190000;
  v.minHeap = 175000;
  v.freePsram = 7000000;
  uint16_t artwork[160 * 160];
  for (unsigned y = 0; y < 160; ++y)
    for (unsigned x = 0; x < 160; ++x)
      artwork[y * 160 + x] = ((x / 6) << 11) | ((y / 3) << 5) | 14;
  v.artwork = artwork;
  v.mediaPixels = artwork;
  v.mediaName = "로컬 영상 테스트";
  v.mediaDuration = 60000;
  v.mediaPosition = 24000;
  v.mediaPlaying = true;
  v.apPassword = "NOVA2468";
  v.updateStatus = "Signature valid. Hold OK on device";
  v.updateReady = true;
  for (unsigned scene = 0; scene < 36; ++scene) {
    if (scene < 18)
      v.screen = static_cast<Screen>(scene);
    else if (scene < 23) {
      v.screen = Screen::Now;
      settings.nowLayout = scene - 18;
      v.canPlay = scene % 2;
    } else if (scene < 35) {
      v.screen = Screen::System;
      v.systemPage = scene - 23;
    } else {
      v.screen = Screen::Media;
      v.sync = true;
      v.syncStale = true;
    }
    ui.render(c, v);
    const std::string name = std::string(argv[1]) + "/screen-" + std::to_string(scene) + ".ppm";
    FILE *f = fopen(name.c_str(), "wb");
    if (!f)
      return 2;
    fprintf(f, "P6\n240 320\n255\n");
    for (int i = 0; i < board::width * board::height; ++i) {
      const uint16_t p = c.pixels()[i];
      const uint8_t rgb[]{uint8_t((p >> 11) * 255 / 31), uint8_t(((p >> 5) & 63) * 255 / 63),
                          uint8_t((p & 31) * 255 / 31)};
      fwrite(rgb, 1, 3, f);
    }
    fclose(f);
  }
  // Theme changes must not recolor matching pixels in artwork or local media.
  artwork[0] = color::accent;
  settings.accentColor = 0xf800;
  settings.mediaMonochrome = false;
  settings.burnin = false;
  v.screen = Screen::Media;
  ui.render(c, v);
  assert(c.pixels()[70 * board::width + 40] == color::accent);
  assert(c.pixels()[280 * board::width + 16] == settings.accentColor);
  settings.mediaMonochrome = true;
  ui.render(c, v);
  assert(c.pixels()[70 * board::width + 40] != color::accent);
  assert(c.pixels()[280 * board::width + 16] == settings.accentColor);
}

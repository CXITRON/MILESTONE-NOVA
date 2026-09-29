#include "Ui.h"
#include <algorithm>
#include <cstdio>
namespace nova {
void formatTime(char *out, size_t n, uint32_t ms) {
  snprintf(out, n, "%02u:%02u", unsigned(ms / 60000), unsigned(ms / 1000 % 60));
}
void progressBar(Canvas &c, int y, uint32_t pos, uint32_t total) {
  c.rect(16, y, 208, 4, color::panel);
  if (total)
    c.rect(16, y, std::min<uint64_t>(208, uint64_t(pos) * 208 / total), 4, c.accent());
}
void Ui::chrome(Canvas &c, const View &v) {
  char top[64];
  snprintf(top, sizeof(top), "%s %s %s", v.wifi ? "W" : "-", v.ble ? "BLE" : "---",
           v.sd ? "SD" : "--");
  c.text(16, 5, 132, 18, top, c.muted());
  char battery[24];
  if (v.batteryValid)
    snprintf(battery, sizeof(battery), "%3u%%", unsigned(v.percent));
  else
    snprintf(battery, sizeof(battery), "--%%");
  c.text(176, 5, 48, 18, battery, v.warning ? color::warning : c.accent());
  c.rect(16, 28, 208, 1, color::panel);
  static constexpr const char *titles[]{
      "CLOCK",         "D-DAY",      "FOCUS",     "ENVIRONMENT",
      "SYSTEM",        "NOW",        "MEDIA",     "SETTINGS",
      "MILESTONE",     "MENU",       "SETUP AP",  "UPDATE",
      "RECOVERY",      "MESSAGE",    "DASHBOARD", "CLOCK + MESSAGE",
      "D-DAY + CLOCK", "DIAGNOSTICS"};
  c.text(16, 39, 208, 20, titles[static_cast<unsigned>(v.screen)], c.accent());
  c.rect(16, 296, 208, 1, color::panel);
  const char *footer = v.screen == Screen::Menu    ? "<> Select  OK  BACK"
                       : v.screen == Screen::Setup ? "BACK Close AP"
                       : v.screen == Screen::Updates || v.screen == Screen::Recovery
                           ? "<> Select  OK / Hold OK"
                       : v.screen == Screen::Settings ? "PREV/NEXT  OK  BACK"
                       : v.screen == Screen::Timer    ? "OK Start/Pause  Hold Reset"
                       : v.screen == Screen::Now
                           ? (v.canPlay ? "OK Play/Pause  Hold Layout" : "<> Layout  제어 불가")
                       : v.screen == Screen::Media
                           ? (v.sync ? "Browser audio   BACK End" : "<> Select  OK Play/Pause")
                           : "< PREV     MENU     NEXT >";
  c.text(12, 300, 216, 18, footer, c.muted(), 1, true);
}
void Ui::render(Canvas &c, const View &v) {
  c.theme(v.settings ? v.settings->accentColor : c.accent(),
          v.settings ? v.settings->mutedColor : c.muted());
  c.clear(color::background);
  if (v.screen == Screen::Boot) {
    c.rect(94, 73, 52, 4, c.accent());
    c.text(0, 112, 240, 48, "NOVA", color::white, 3, true);
    c.text(0, 170, 240, 20, "MILESTONE", c.accent(), 1, true);
    c.text(16, 223, 208, 40, "오늘도 한 걸음", c.muted(), 1, true);
    return;
  }
  chrome(c, v);
  if (v.sleeping) {
    c.text(16, 110, 208, 60, "전원을 끄는 중", color::white, 1, true);
    c.text(16, 200, 208, 50, "OK 버튼을 놓아 주세요", c.muted(), 1, true);
    return;
  }
  switch (v.screen) {
  case Screen::Menu: {
    static constexpr const char *labels[]{"CORE",     "MEDIA", "NOW",  "기기 설정", "설정 AP",
                                          "업데이트", "복구",  "진단", "재시작",    "전원 OFF"};
    const unsigned start = v.menu > 6 ? v.menu - 6 : 0;
    for (unsigned i = start; i < std::min(start + 7, unsigned(MenuAction::Count)); ++i) {
      const int y = 76 + (i - start) * 28;
      if (i == v.menu)
        c.rect(10, y - 3, 220, 27, color::panel);
      c.text(18, y, 204, 22, labels[i], i == v.menu ? c.accent() : c.muted());
    }
    break;
  }
  case Screen::Setup:
    c.text(16, 78, 208, 36, "MILESTONE-NOVA-SETUP", color::white, 1, true);
    c.text(16, 128, 208, 24, "AP PASSWORD", c.muted(), 1, true);
    c.text(16, 155, 208, 60, v.apPassword[0] ? v.apPassword : "OPEN · 암호 없음", c.accent(), 1,
           true);
    c.text(16, 222, 208, 24, "http://192.168.4.1", color::white, 1, true);
    c.text(16, 254, 208, 22, "BACK으로 AP 종료", c.muted(), 1, true);
    break;
  case Screen::Updates:
  case Screen::Recovery: {
    static constexpr const char *items[]{"인터넷 버전 확인", "서명 후보 다운로드", "SD 후보 검증",
                                         "이전 검증 펌웨어 복구"};
    for (unsigned i = 0; i < 4; ++i) {
      const int y = 78 + i * 29;
      if (i == v.recoveryItem)
        c.rect(10, y - 3, 220, 27, color::panel);
      c.text(18, y, 204, 22, items[i], i == v.recoveryItem ? c.accent() : c.muted());
    }
    c.text(16, 204, 208, 52, v.updateStatus, color::white, 1, true);
    c.text(16, 260, 208, 26, v.updateReady ? "길게 OK: 설치 확인" : "복구: 항목 선택 후 길게 OK",
           color::warning, 1, true);
    progressBar(c, 289, v.updateProgress, 100);
    break;
  }
  case Screen::Now:
    nowScreen(c, v);
    break;
  case Screen::Media:
    mediaScreen(c, v);
    break;
  case Screen::Settings:
    settingsScreen(c, v);
    break;
  default:
    coreScreen(c, v);
    break;
  }
  if (v.syncStale && v.screen == Screen::Media)
    c.text(16, 263, 208, 20, "동기 신호 끊김 · 정지", color::warning, 1, true);
  if (v.notice && v.notice[0]) {
    c.rect(8, 264, 224, 29, color::panel);
    c.text(14, 268, 212, 20, v.notice, color::warning);
  }
  if (v.settings) {
    auto *pixels = c.pixels();
    for (unsigned i = 0; i < board::width * board::height; ++i) {
      uint16_t p = pixels[i];
      if (v.settings->luminance != 100 || v.settings->contrast) {
        const auto tone = [&](int value, int max) {
          int channel = value * 255 / max;
          channel = (channel - 128) * (100 + v.settings->contrast) / 100 + 128;
          return std::clamp(channel * int(v.settings->luminance) / 100, 0, 255) * max / 255;
        };
        p = (tone(p >> 11, 31) << 11) | (tone((p >> 5) & 63, 63) << 5) | tone(p & 31, 31);
      }
      pixels[i] = p;
    }
    if (v.settings->burnin && (v.now / 60000) % 2) {
      for (int y = board::height - 1; y > 0; --y)
        for (int x = board::width - 1; x > 0; --x)
          pixels[y * board::width + x] = pixels[(y - 1) * board::width + x - 1];
      for (int x = 0; x < board::width; ++x)
        pixels[x] = color::background;
      for (int y = 0; y < board::height; ++y)
        pixels[y * board::width] = color::background;
    }
  }
}
void mediaScreen(Canvas &c, const View &v) {
  if (v.mediaPixels)
    c.image(40, 70, 160, 160, v.mediaPixels, v.settings->mediaMonochrome);
  else {
    c.rect(40, 70, 160, 160, color::panel);
    c.text(48, 134, 144, 60, "미디어 없음", c.muted(), 1, true);
  }
  c.text(16, 236, 208, 20, v.mediaName, color::white, 1, true);
  progressBar(c, 280, v.mediaPosition, v.mediaDuration);
}
} // namespace nova

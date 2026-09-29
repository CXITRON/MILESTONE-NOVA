#include "Ui.h"
#include <cstdio>
namespace nova {
namespace {
void row(Canvas &c, int y, const char *label, const char *value) {
  c.text(16, y, 96, 20, label, c.muted());
  c.text(112, y, 112, 20, value, color::white);
}
void message(Canvas &c, const View &v, int y, int height = 40) {
  const auto &s = *v.settings;
  if (s.scroll)
    c.marquee(24, y, 192, s.message, s.messageColor, v.now, s.scrollSpeed, !s.alignLeft);
  else
    c.text(24, y, 192, height, s.message, s.messageColor, 1, !s.alignLeft);
}
void diagnostic(Canvas &c, const View &v) {
  char value[128];
  snprintf(value, sizeof(value), "%02u / 12", v.systemPage + 1);
  c.text(148, 39, 76, 20, value, c.muted(), 1, true);
  switch (v.systemPage) {
  case 0:
    row(c, 86, "FIRMWARE", board::version);
    row(c, 120, "BOARD", "LOLIN S3 Pro");
    row(c, 154, "TARGET", "ESP32-S3");
    row(c, 188, "DISPLAY", "240 x 320");
    row(c, 222, "ARCH", "Single MCU");
    break;
  case 1:
    row(c, 86, "WI-FI", v.wifiStatus);
    row(c, 122, "IP", v.ip);
    row(c, 158, "BLE", v.bleStatus);
    row(c, 194, "MODE",
        v.mode == Profile::Now     ? "NOW"
        : v.mode == Profile::Media ? "MEDIA"
                                   : "CORE");
    break;
  case 2:
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(v.freeHeap));
    row(c, 90, "FREE HEAP", value);
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(v.minHeap));
    row(c, 130, "MIN HEAP", value);
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(v.freePsram));
    row(c, 170, "PSRAM", value);
    break;
  case 3:
    snprintf(value, sizeof(value), "%lu B", static_cast<unsigned long>(v.stack));
    row(c, 92, "STACK FREE", value);
    snprintf(value, sizeof(value), "%lu us", static_cast<unsigned long>(v.loopMaxUs));
    row(c, 136, "LOOP MAX", value);
    snprintf(value, sizeof(value), "%lu s", static_cast<unsigned long>(v.uptime));
    row(c, 180, "UPTIME", value);
    break;
  case 4:
    row(c, 92, "SD", v.sd ? "Mounted" : "Unavailable");
    row(c, 128, "MEDIA", v.mediaPlaying ? "Playing" : "Paused");
    formatTime(value, sizeof(value), v.mediaPosition);
    row(c, 164, "POSITION", value);
    formatTime(value, sizeof(value), v.mediaDuration);
    row(c, 200, "DURATION", value);
    break;
  case 5:
    row(c, 92, "RTC", v.rtc ? "DS3231 ready" : "Unavailable");
    row(c, 128, "TIME", v.epoch >= 1704067200 ? "Valid" : "Needs sync");
    row(c, 164, "ZONE", v.settings->timezone);
    break;
  case 6:
    snprintf(value, sizeof(value), "%.3f V", v.volts);
    row(c, 92, "BATTERY", v.batteryValid ? value : "Unknown");
    snprintf(value, sizeof(value), "%.0f %%", v.percent);
    row(c, 132, "ESTIMATE", value);
    row(c, 172, "ADC", "Calibrated x2");
    row(c, 212, "WARNING", v.warning ? "Active" : "No");
    break;
  case 7:
    snprintf(value, sizeof(value), "%.1f C", v.temperature);
    row(c, 92, "AMBIENT", v.environmentValid ? value : "Unavailable");
    snprintf(value, sizeof(value), "%.1f %%", v.humidity);
    row(c, 132, "HUMIDITY", v.environmentValid ? value : "Unavailable");
    snprintf(value, sizeof(value), "%lu ms", static_cast<unsigned long>(v.settings->sampleMs));
    row(c, 172, "INTERVAL", value);
    break;
  case 8:
    snprintf(value, sizeof(value), "%.1f C", v.chipTemperature);
    row(c, 92, "CHIP TEMP", value);
    snprintf(value, sizeof(value), "%d", v.thermalState);
    row(c, 132, "THERMAL", value);
    c.text(16, 190, 208, 64, "내부 칩 온도는 실내 온도와 다릅니다.", c.muted(), 1, true);
    break;
  case 9:
    row(c, 86, "AMS", v.bleStatus);
    row(c, 126, "PLAY/PAUSE", v.canPlay ? "Supported" : "Unavailable");
    row(c, 166, "PREV/NEXT", v.canPrev && v.canNext ? "Supported" : "Limited / none");
    row(c, 206, "POSITION", v.media && v.media->positionKnown() ? "Known" : "Unknown");
    break;
  case 10:
    row(c, 86, "OTA", v.otaStatus);
    snprintf(value, sizeof(value), "%u %%", v.otaPercent);
    row(c, 126, "PROGRESS", value);
    c.text(16, 168, 208, 90, v.updateStatus, color::white);
    break;
  case 11:
    snprintf(value, sizeof(value), "%lu MHz",
             static_cast<unsigned long>(v.settings->lcdHz / 1000000));
    row(c, 86, "LCD SPI", value);
    row(c, 126, "SD SPI", "10 MHz");
    row(c, 166, "WAKE", "OK / GPIO5");
    row(c, 206, "AP SYNC", v.sync ? (v.syncStale ? "Stale / stopped" : "Active") : "Inactive");
    break;
  }
  c.text(16, 264, 208, 24, "OK / 길게 <> 정보 페이지", c.muted(), 1, true);
}
} // namespace
void coreScreen(Canvas &c, const View &v) {
  const auto &s = *v.settings;
  char text[128], clock[24], date[40], dday[64];
  tm local{};
  const bool valid = v.epoch >= 1704067200;
  if (valid) {
    localtime_r(&v.epoch, &local);
    strftime(clock, sizeof(clock),
             s.hour24 ? (s.showSeconds ? "%H:%M:%S" : "%H:%M")
                      : (s.showSeconds ? "%I:%M:%S %p" : "%I:%M %p"),
             &local);
    strftime(date, sizeof(date), "%Y.%m.%d  %a", &local);
    const int days = civilDay(s.ddayYear, s.ddayMonth, s.ddayDay) -
                     civilDay(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    if (days < 0 && s.afterComplete)
      snprintf(dday, sizeof(dday), "완료");
    else if (!days)
      snprintf(dday, sizeof(dday), "D-DAY");
    else if (s.ddayText)
      snprintf(dday, sizeof(dday), days > 0 ? "%d일 남음" : "%d일 지남", days > 0 ? days : -days);
    else
      snprintf(dday, sizeof(dday), "D%c%d", days > 0 ? '-' : '+', days > 0 ? days : -days);
  } else {
    snprintf(clock, sizeof(clock), "--:--");
    snprintf(date, sizeof(date), "시간 설정 필요");
    snprintf(dday, sizeof(dday), "D-?");
  }
  switch (v.screen) {
  case Screen::Clock:
  case Screen::DateMessage:
    c.text(16, 84, 208, 24, s.label, s.accentColor, 1, true);
    c.text(0, 124, 240, 58, clock, s.timeColor, s.showSeconds || !s.hour24 ? 2 : 3, true);
    c.text(0, 198, 240, 24, date, s.dateColor, 1, true);
    if (v.screen == Screen::DateMessage)
      message(c, v, 244);
    break;
  case Screen::DDay:
    c.text(16, 82, 208, 24, s.label, s.accentColor, 1, true);
    c.text(0, 123, 240, 58, dday, s.eventColor, 2, true);
    snprintf(text, sizeof(text), "%04u.%02u.%02u", s.ddayYear, s.ddayMonth, s.ddayDay);
    c.text(0, 190, 240, 24, text, s.dateColor, 1, true);
    message(c, v, 239);
    break;
  case Screen::Message:
    c.text(16, 88, 208, 24, s.label, s.accentColor, 1, true);
    if (s.scroll)
      message(c, v, 158);
    else
      c.text(24, 130, 192, 128, s.message, s.messageColor, 2, !s.alignLeft);
    break;
  case Screen::DDayClock:
    c.text(16, 76, 208, 24, s.label, s.accentColor, 1, true);
    c.text(0, 112, 240, 52, dday, s.eventColor, 2, true);
    c.text(0, 184, 240, 44, clock, s.timeColor, 2, true);
    c.text(0, 244, 240, 24, date, s.dateColor, 1, true);
    break;
  case Screen::Dashboard:
    c.text(16, 78, 208, 44, clock, s.timeColor, 2, true);
    c.text(16, 127, 208, 22, date, s.dateColor, 1, true);
    c.text(16, 164, 208, 42, dday, s.eventColor, 2, true);
    message(c, v, 216);
    snprintf(text, sizeof(text), v.environmentValid ? "%.1f C · %.0f %%" : "AHT10 대기",
             v.temperature, v.humidity);
    c.text(16, 257, 208, 22, text, s.mutedColor, 1, true);
    break;
  case Screen::Timer: {
    formatTime(text, sizeof(text), v.timer->remaining(v.now) + 999);
    c.text(0, 108, 240, 60, text, s.timeColor, 3, true);
    const char *state = v.timer->state() == FocusTimer::State::Running    ? "집중하는 시간"
                        : v.timer->state() == FocusTimer::State::Finished ? "수고했어요"
                        : v.timer->state() == FocusTimer::State::Paused   ? "일시 정지"
                                                                          : "준비되면 OK";
    c.text(0, 188, 240, 40, state, s.accentColor, 1, true);
    const uint32_t total = s.focusSeconds * 1000, remaining = v.timer->remaining(v.now);
    progressBar(c, 248, total > remaining ? total - remaining : 0, total);
    break;
  }
  case Screen::Environment: {
    const float shown = s.fahrenheit ? v.temperature * 1.8f + 32 : v.temperature;
    const bool warning = v.temperature < s.temperatureLow || v.temperature > s.temperatureHigh ||
                         v.humidity < s.humidityLow || v.humidity > s.humidityHigh,
               critical =
                   v.temperature >= s.temperatureCritical || v.humidity >= s.humidityCritical;
    const auto ink = critical ? color::danger : warning ? color::warning : s.timeColor;
    if (s.environmentMask & 1) {
      if (v.environmentValid)
        snprintf(text, sizeof(text), "%.1f %s", shown, s.fahrenheit ? "F" : "C");
      else
        snprintf(text, sizeof(text), "-- %s", s.fahrenheit ? "F" : "C");
      c.text(16, 104, 208, 42, text, ink, 2, true);
    }
    if (s.environmentMask & 2) {
      snprintf(text, sizeof(text), v.environmentValid ? "%.1f %%" : "-- %%", v.humidity);
      c.text(16, 169, 208, 42, text, s.accentColor, 2, true);
    }
    c.text(16, 246, 208, 36,
           !v.environmentValid ? "AHT10 응답 대기 중"
           : critical          ? "환경 위험 임계값"
           : warning           ? "환경 권장 범위 벗어남"
                               : "TEMPERATURE / HUMIDITY",
           v.environmentValid && warning ? color::warning : s.mutedColor, 1, true);
    break;
  }
  case Screen::System:
  case Screen::Diagnostics:
    diagnostic(c, v);
    break;
  default:
    break;
  }
}
} // namespace nova

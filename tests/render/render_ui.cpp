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
  strcpy(track.album, "Preview Album");
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
  v.stack = 6144;
  v.loopMaxUs = 3400;
  v.uptime = 86400;
  v.chipTemperature = 46.2f;
  // Same sample gradient at the device's artwork (200) and full-width media (240) sides.
  static uint16_t artwork[board::artSide * board::artSide], mediaFrame[board::mediaSide * board::mediaSide];
  for (unsigned y = 0; y < board::artSide; ++y)
    for (unsigned x = 0; x < board::artSide; ++x)
      artwork[y * board::artSide + x] = ((x * 32 / board::artSide) << 11) |
                                        ((y * 64 / board::artSide) << 5) | 14;
  for (unsigned y = 0; y < board::mediaSide; ++y)
    for (unsigned x = 0; x < board::mediaSide; ++x)
      mediaFrame[y * board::mediaSide + x] = ((x * 32 / board::mediaSide) << 11) |
                                        ((y * 64 / board::mediaSide) << 5) | 14;
  v.artwork = artwork;
  v.mediaPixels = mediaFrame;
  v.mediaName = "로컬 영상 테스트";
  v.mediaDuration = 60000;
  v.mediaPosition = 24000;
  v.mediaPlaying = true;
  v.apPassword = "NOVA2468";
  v.updateStatus = "Signature valid. Hold OK on device";
  v.updateReady = true;
  const Settings defaults = settings;
  const View base = v;
  const std::string manifestPath = std::string(argv[1]) + "/scenes.tsv";
  FILE *manifest = fopen(manifestPath.c_str(), "w");
  if (!manifest)
    return 2;
  unsigned count = 0;
  const auto reset = [&](Screen screen) {
    settings = defaults;
    v = base;
    v.screen = screen;
    v.mode = screen == Screen::Now     ? Profile::Now
             : screen == Screen::Media ? Profile::Media
                                       : Profile::Core;
  };
  const auto save = [&](const char *group, const char *title) {
    ui.render(c, v);
    const std::string file = "screen-" + std::to_string(count++) + ".ppm";
    const std::string name = std::string(argv[1]) + "/" + file;
    FILE *f = fopen(name.c_str(), "wb");
    if (!f)
      std::exit(2);
    fprintf(f, "P6\n240 320\n255\n");
    for (int i = 0; i < board::width * board::height; ++i) {
      const uint16_t p = c.pixels()[i];
      const uint8_t rgb[]{uint8_t((p >> 11) * 255 / 31), uint8_t(((p >> 5) & 63) * 255 / 63),
                          uint8_t((p & 31) * 255 / 31)};
      fwrite(rgb, 1, 3, f);
    }
    if (fclose(f))
      std::exit(2);
    fprintf(manifest, "%s\t%s\t%s\n", file.c_str(), group, title);
  };
  static constexpr const char *titles[]{"시계",        "D-Day",          "집중 타이머 · 실행",
                                        "온도 / 습도", "시스템 정보",    "NOW · 가사",
                                        "로컬 미디어", "기기 설정",      "부팅",
                                        "메뉴 · 상단", "설정 AP · 암호", "업데이트",
                                        "복구",        "사용자 문구",    "대시보드",
                                        "시계 + 문구", "D-Day + 시계",   "진단 · 첫 페이지"};
  static constexpr const char *groups[]{"CORE",
                                        "CORE",
                                        "CORE",
                                        "CORE",
                                        "진단",
                                        "NOW",
                                        "MEDIA / SYNC",
                                        "메뉴 / 설정",
                                        "메뉴 / 설정",
                                        "메뉴 / 설정",
                                        "메뉴 / 설정",
                                        "업데이트 / 전원",
                                        "업데이트 / 전원",
                                        "CORE",
                                        "CORE",
                                        "CORE",
                                        "CORE",
                                        "진단"};
  for (unsigned screen = 0; screen < 18; ++screen) {
    reset(static_cast<Screen>(screen));
    save(groups[screen], titles[screen]);
  }
  static constexpr const char *layouts[]{"아트 + 아티스트 / 제목 · 제어 불가",
                                         "작은 아트 + 상세 · 제어 가능", "텍스트 중심 · 제어 불가",
                                         "큰 아트 + 앨범 · 제어 가능", "동기화 가사 · 제어 불가",
                                         "아트 + 가사 · 제어 가능"};
  static_assert(sizeof(layouts) / sizeof(*layouts) == nowLayouts);
  for (unsigned layout = 0; layout < nowLayouts; ++layout) {
    reset(Screen::Now);
    settings.nowLayout = layout;
    v.canPlay = layout % 2;
    save("NOW", layouts[layout]);
  }
  static constexpr const char *pages[]{
      "01 · 펌웨어 / 보드", "02 · Wi-Fi / BLE",   "03 · 메모리", "04 · 실행 시간 / 스택",
      "05 · SD / 미디어",   "06 · RTC / 시간대",  "07 · 배터리", "08 · 환경 센서",
      "09 · 칩 온도",       "10 · AMS 지원 명령", "11 · OTA",    "12 · SPI / Wake / Sync"};
  for (unsigned page = 0; page < 12; ++page) {
    reset(Screen::System);
    v.systemPage = page;
    save("진단", pages[page]);
  }
  reset(Screen::Media);
  v.sync = v.syncStale = true;
  save("MEDIA / SYNC", "AP Sync · 신호 끊김");

  reset(Screen::Menu);
  v.menu = unsigned(MenuAction::Off);
  save("메뉴 / 설정", "메뉴 · 하단 / OFF 선택");
  reset(Screen::Settings);
  v.settingsEditing = true;
  save("메뉴 / 설정", "기기 설정 · 값 편집");
  reset(Screen::Settings);
  v.sleeping = true;
  save("업데이트 / 전원", "OFF · 종료 대기");

  MediaSession empty, paused = media, unknown;
  paused.synchronize(134000, false, v.now);
  unknown.replace(track, v.now);
  Lyrics unsynced, instrumental;
  LrcParser{}.parse("동기화 정보 없는 가사\n두 번째 줄\n세 번째 줄", unsynced);
  LrcParser{}.parse("[02:10]이전 줄\n[02:13]\n[02:18]다음 줄", instrumental);
  reset(Screen::Now);
  v.media = &empty;
  v.artwork = nullptr;
  v.lyrics = nullptr;
  v.ble = false;
  save("NOW", "연결 / 미디어 수신 대기");
  reset(Screen::Now);
  settings.nowLayout = 1;
  v.media = &paused;
  v.canPlay = true;
  save("NOW", "일시 정지 · 제어 가능");
  reset(Screen::Now);
  settings.nowLayout = 0;
  v.artwork = nullptr;
  save("NOW", "앨범아트 없음");
  reset(Screen::Now);
  v.lyrics = nullptr;
  save("NOW", "로컬 가사 없음");
  reset(Screen::Now);
  v.lyrics = &unsynced;
  save("NOW", "시간 정보 없는 가사");
  reset(Screen::Now);
  v.media = &unknown;
  save("NOW", "재생 위치 수신 대기");
  MediaSession before = media;
  before.synchronize(10000, true, v.now);
  reset(Screen::Now);
  v.media = &before;
  save("NOW", "첫 가사 시작 전");
  reset(Screen::Now);
  v.lyrics = &instrumental;
  save("NOW", "간주 · 빈 가사 구간");
  reset(Screen::Now);
  settings.nowLayout = 5;
  v.lyrics = nullptr;
  save("NOW", "아트 + 가사 · 가사 없음");
  reset(Screen::Now);
  settings.nowLayout = 5;
  v.lyrics = &unsynced;
  save("NOW", "아트 + 가사 · 시간 정보 없음");

  reset(Screen::Media);
  v.mediaPixels = nullptr;
  v.mediaName = "No media";
  v.mediaDuration = v.mediaPosition = 0;
  v.mediaPlaying = false;
  v.sd = false;
  save("MEDIA / SYNC", "미디어 / SD 없음");
  reset(Screen::Media);
  settings.mediaMonochrome = true;
  save("MEDIA / SYNC", "로컬 미디어 · 흑백");
  reset(Screen::Media);
  v.sync = true;
  save("MEDIA / SYNC", "AP Sync · 연결됨");

  reset(Screen::Clock);
  v.epoch = 0;
  v.rtc = false;
  save("CORE", "시간 설정 전");
  reset(Screen::Environment);
  v.environmentValid = false;
  save("CORE", "환경 센서 응답 대기");
  reset(Screen::Environment);
  v.temperature = 32;
  save("CORE", "환경 권장 범위 경고");
  reset(Screen::Environment);
  v.temperature = 41;
  save("CORE", "환경 위험 임계값");
  FocusTimer ready, timerPaused = timer, finished;
  ready.reset(settings.focusSeconds);
  timerPaused.toggle(v.now);
  finished.reset(60);
  finished.toggle(0);
  finished.tick(60000);
  assert(finished.state() == FocusTimer::State::Finished);
  reset(Screen::Timer);
  v.timer = &ready;
  save("CORE", "집중 타이머 · 시작 전");
  reset(Screen::Timer);
  v.timer = &timerPaused;
  save("CORE", "집중 타이머 · 일시 정지");
  reset(Screen::Timer);
  v.timer = &finished;
  save("CORE", "집중 타이머 · 완료");
  reset(Screen::Setup);
  v.apPassword = "";
  save("메뉴 / 설정", "설정 AP · 공개 모드");
  reset(Screen::Updates);
  v.updateReady = false;
  v.updateProgress = 48;
  v.recoveryItem = 1;
  v.updateStatus = "Downloading signed firmware...";
  save("업데이트 / 전원", "업데이트 · 다운로드 중");
  reset(Screen::Recovery);
  v.updateReady = false;
  v.recoveryItem = 2;
  v.updateStatus = "Signature verification failed";
  save("업데이트 / 전원", "업데이트 · 서명 검증 실패");
  if (fclose(manifest))
    return 2;
  printf("Rendered %u scenes using the firmware UI\n", count);

  // Theme changes must not recolor matching pixels in artwork or local media.
  reset(Screen::Media);
  mediaFrame[0] = color::accent;
  settings.accentColor = 0xf800;
  settings.mediaMonochrome = false;
  settings.burnin = false;
  v.screen = Screen::Media;
  ui.render(c, v);
  assert(c.pixels()[30 * board::width] == color::accent);
  assert(c.pixels()[291 * board::width + 16] == settings.accentColor);
  settings.mediaMonochrome = true;
  ui.render(c, v);
  assert(c.pixels()[30 * board::width] != color::accent);
  assert(c.pixels()[291 * board::width + 16] == settings.accentColor);
}

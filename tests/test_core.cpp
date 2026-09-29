#include "board/Board.h"
#include "core/Logic.h"
#include "core/Text.h"
#include "input/Button.h"
#include "lyrics/Lyrics.h"
#include "media/AmsDecoder.h"
#include "media/HelperProtocol.h"
#include "media/Session.h"
#include "settings/Values.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <random>
#include <vector>
using namespace nova;
namespace {
unsigned checks = 0;
void check(bool condition) {
  ++checks;
  if (!condition) {
    std::cerr << "Check " << checks << " failed\n";
    std::abort();
  }
}
void textTests() {
  char text[64];
  check(validUtf8("한글 English 한A글"));
  check(validUtf8("\xEF\xBF\xBD"));
  cleanUtf8(text, sizeof(text), "\xEF\xBF\xBD");
  check(std::string(text) == "\xEF\xBF\xBD");
  check(!validUtf8("\xC0\xAF"));
  check(!validUtf8("\xED\xA0\x80"));
  check(!validUtf8("\xF4\x90\x80\x80"));
  cleanUtf8(text, sizeof(text), "한\xFF글");
  check(std::string(text) == "한?글");
  cleanUtf8(text, 6, "한글");
  check(std::string(text) == "한");
  cleanUtf8(text, 1, "한글");
  check(text[0] == 0);
  sanitizeFilename(text, sizeof(text), "../한글:a?b");
  check(std::string(text) == ".._한글_a_b");
  sanitizeFilename(text, sizeof(text), "..");
  check(std::string(text) == "untitled");
  char key[17], second[17];
  trackKey(key, "ab", "c", "", 1000);
  trackKey(second, "a", "bc", "", 1000);
  check(strcmp(key, second) != 0);
  check(validTrackKey(key));
  trackKey(key, "한글 Artist", "NOVA", "앨범", 231000);
  check(!strcmp(key, "51877402f0e5f146")); // Shared with the Python tool fixture.
  check(!validTrackKey("../../not-a-key!!"));
  check(crc32("123456789", 9) == 0xCBF43926);
}
void logicTests() {
  check(board::pinsValid());
  check(!board::allowed(4));
  check(!board::allowed(33));
  check(validDate(2000, 2, 29));
  check(!validDate(2100, 2, 29));
  check(!validDate(2026, 4, 31));
  check(civilDay(2026, 12, 31) + 1 == civilDay(2027, 1, 1));
  check(civilDay(2000, 3, 1) - civilDay(2000, 2, 28) == 2);
  check(batteryPercent(3.2f) == 0);
  check(batteryPercent(4.2f) == 100);
  check(batteryPercent(NAN) == 0);
  for (int i = 3000; i < 4400; ++i)
    check(batteryPercent(i / 1000.0f) <= batteryPercent((i + 1) / 1000.0f));
  check(batteryPercent(3.78f) == 40);
  check(batteryPercent(3.72f) < 30);
  Settings s;
  check(validSettings(s));
  check(!applySetting(s, "lcd_hz", "40000001"));
  check(!applySetting(s, "lcd_brightness", "256"));
  check(!applySetting(s, "rgb_brightness", "-1"));
  check(!applySetting(s, "battery_gain", "nan"));
  check(!applySetting(s, "focus_seconds", "1.5"));
  check(!applySetting(s, "dday", "2026-02-29"));
  check(applySetting(s, "dday", "2028-02-29"));
  check(applySetting(s, "message", "한글과 English"));
  check(validSettings(s));
  s.batteryOffset = std::numeric_limits<float>::infinity();
  check(!validSettings(s));
  FocusTimer timer;
  timer.reset(60);
  timer.toggle(0xFFFFFF00U);
  timer.tick(0x000000F4U);
  check(timer.remaining(0x000000F4U) == 59500);
  timer.toggle(0x000000F4U);
  check(timer.state() == FocusTimer::State::Paused);
  check(timer.remaining(10000) == 59500);
  timer.toggle(10000);
  timer.tick(69500);
  check(timer.state() == FocusTimer::State::Finished);
  check(timer.remaining(70000) == 0);
}
void buttonTests() {
  Button b;
  b.begin(true, 0);
  check(b.sample(true, 5000) == Press::None);
  b.sample(false, 5010);
  check(b.sample(false, 5040) == Press::None);
  b.sample(true, 6000);
  check(b.sample(true, 6029) == Press::None);
  b.sample(true, 6030);
  b.sample(false, 6100);
  check(b.sample(false, 6130) == Press::Short);
  b.sample(true, 7000);
  b.sample(true, 7030);
  check(b.sample(true, 7930, true) == Press::Long);
  check(b.sample(true, 8110, true) == Press::Repeat);
  b.sample(false, 8200);
  check(b.sample(false, 8230) == Press::None);
  b.begin(false, 0xFFFFFFF0U);
  b.sample(true, 0xFFFFFFF5U);
  check(b.sample(true, 25) == Press::None);
  b.sample(false, 50);
  check(b.sample(false, 80) == Press::Short);
}
void lyricTests() {
  uint32_t stamp;
  check(parseTimestamp("12:34.56", stamp) && stamp == 754560);
  check(parseTimestamp("00:01.234", stamp) && stamp == 1234);
  check(parseTimestamp("0:00.7", stamp) && stamp == 700);
  check(!parseTimestamp("01:60", stamp));
  check(!parseTimestamp("1:2", stamp));
  check(!parseTimestamp("-1:20", stamp));
  check(!parseTimestamp("00:00.1234", stamp));
  check(!parseTimestamp("00:0x", stamp));
  Lyrics lyrics;
  LrcParser parser;
  LyricsTimeline timeline;
  parser.parse("\xEF\xBB\xBF[ar:example]\r\n[00:01.20][00:02.345]첫 번째 줄\r\n[00:03]Mixed "
               "한글\n[00:04]\n[offset:-200]\n[00:03]last duplicate\n[bad\n[99:99]bad\n",
               lyrics);
  check(lyrics.synced && lyrics.count == 5 && lyrics.rejected == 2);
  check(lyrics.lines[0].timeMs == 1000);
  check(lyrics.lines[1].timeMs == 2145);
  check(timeline.index(lyrics, 999) == -1);
  check(timeline.index(lyrics, 1000) == 0);
  check(timeline.index(lyrics, 2800) == 3);
  check(lyrics.line(3) == "last duplicate");
  check(timeline.index(lyrics, 5000) == 4);
  check(lyrics.line(4).empty());
  check(timeline.index(lyrics, 1100) == 0);
  parser.parse("line one\n두 번째 줄\n", lyrics);
  check(!lyrics.synced && lyrics.count == 2);
  check(timeline.index(lyrics, 10000) == -1);
  parser.parse("[offset:+150]\n[00:00.100]a\n[00:00.2]b", lyrics);
  check(lyrics.lines[0].timeMs == 250);
  parser.parse("[00:00.0]a\n[offset:99999999999]", lyrics);
  check(lyrics.rejected == 1 && lyrics.lines[0].timeMs == 0);
  std::string huge;
  for (int n = 0; n < 1000; ++n)
    huge += "[00:01]a\n";
  parser.parse(huge, lyrics);
  check(lyrics.truncated && lyrics.count == 512);
  std::mt19937 generator(512);
  for (unsigned iteration = 0; iteration < 1200; ++iteration) {
    std::string input(generator() % 2048, ' ');
    for (auto &c : input)
      c = static_cast<char>(generator());
    parser.parse(input, lyrics);
    check(lyrics.count <= maxLyricsLines && lyrics.used <= maxLyricsBytes);
    for (size_t n = 0; n < lyrics.count; ++n)
      check(lyrics.lines[n].start + lyrics.lines[n].length < maxLyricsBytes);
  }
}
void mediaTests() {
  Track t;
  strcpy(t.title, "Sample");
  t.durationMs = 200000;
  MediaSession session;
  check(session.replace(t, 0));
  session.synchronize(10000, true, 1000);
  check(session.position(2500) == 11500);
  session.synchronize(11500, false, 2500);
  check(session.position(6000) == 11500);
  session.synchronize(5000, true, 6000);
  check(session.position(6200) == 5200);
  session.synchronize(1000, true, 0xFFFFFF00U);
  check(session.position(0x000000F4U) == 1500);
  session.disconnect(0x00000158U);
  check(!session.playing());
  check(session.position(4000) == 1600);
  t.durationMs = 10000;
  strcpy(t.title, "New track");
  check(session.replace(t, 5000));
  check(!session.positionKnown() && session.position(6000) == 0);
  session.synchronize(9999, true, 6000);
  check(session.position(600000) == 10000);
  session.synchronize(50, true, 1000, NAN);
  check(session.position(2000) == 1050);
  HelperDecoder decoder;
  const auto packet = [&](uint8_t type, const std::vector<uint8_t> &payload,
                          uint32_t transaction = 7, uint32_t now = 100) {
    std::vector<uint8_t> bytes{1,
                               type,
                               uint8_t(transaction),
                               uint8_t(transaction >> 8),
                               uint8_t(transaction >> 16),
                               uint8_t(transaction >> 24)};
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return decoder.accept(bytes.data(), bytes.size(), now);
  };
  check(!packet(1, {}));
  check(!packet(2, {'t'}));
  check(!packet(3, {}));
  check(!packet(4, {}));
  check(!packet(5, {0xE8, 3, 0, 0, 0x10, 0x27, 0, 0, 1}));
  check(packet(6, {}));
  check(decoder.position() == 1000 && decoder.playing() && decoder.track().durationMs == 10000);
  packet(1, {});
  packet(2, {'t'});
  check(!packet(6, {}));
  packet(1, {});
  check(!packet(2, {'t'}, 8));
  check(!packet(6, {}));
  packet(1, {});
  check(!packet(2, {'t'}, 7, 6000));
  check(!packet(6, {}));
}
void amsTests() {
  AmsDecoder decoder;
  MediaSession session;
  const auto send = [&](uint8_t entity, uint8_t field, const char *text, uint32_t at) {
    std::vector<uint8_t> packet{entity, field, 0};
    packet.insert(packet.end(), text, text + strlen(text));
    decoder.accept(packet.data(), packet.size(), at, session);
  };
  send(2, 2, "첫 곡", 100);
  send(0, 1, "1,1.0,12.5", 150);
  send(2, 0, "Artist", 200);
  send(2, 3, "60.0", 220);
  decoder.tick(619, session);
  check(session.generation() == 0);
  decoder.tick(620, session);
  check(session.generation() == 1 && session.positionKnown());
  check(session.position(1150) == 13500);
  check(session.track().durationMs == 60000);
  send(0, 1, "2,0.0,14.0", 1200);
  check(!session.playing() && session.position(9000) == 14000);
  send(0, 1, "1,1.0,3.0", 2000);
  check(session.position(3000) == 4000);
  send(2, 2, "두 번째 곡", 5000);
  decoder.tick(5400, session);
  check(session.generation() == 2 && !session.positionKnown());
  send(0, 1, "1,1.0,0.5", 5500);
  check(session.position(6000) == 1000);
  send(0, 1, "1,nan,0.0", 6500);
  check(session.position(6500) == 1500);
  send(2, 3, "not-a-duration", 6600);
  decoder.tick(7200, session);
  check(session.track().durationMs == 60000);
  decoder.reset();
  send(0, 1, "1,1.0,1.0", 0xFFFFFE00U);
  send(2, 2, "wrap", 0xFFFFFF00U);
  decoder.tick(0xA0U, session);
  check(session.position(0x1E8U) == 2000);
}
} // namespace
int main() {
  textTests();
  logicTests();
  buttonTests();
  lyricTests();
  mediaTests();
  amsTests();
  std::cout << checks << " checks passed\n";
}

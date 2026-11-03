#include "board/Board.h"
#include "core/Logic.h"
#include "core/Text.h"
#include "input/Button.h"
#include "input/InputEvents.h"
#include "lyrics/Lyrics.h"
#include "media/AmsDecoder.h"
#include "media/HelperProtocol.h"
#include "media/Session.h"
#include "memo/Morse.h"
#include "settings/Values.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <random>
#include <string>
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
void inputBacklogTests() {
  InputEvents keys;
  std::array<bool, 5> pressed{};
  InputEvent event;
  keys.begin(pressed, 0);
  // Producer keeps sampling during a 600 ms render stall; the consumer runs only at the end.
  for (uint32_t now = 0; now <= 600; now += 5) {
    pressed[0] = now >= 70 && now < 160;
    pressed[2] = now >= 250 && now < 340;
    pressed[4] = now >= 430 && now < 520;
    keys.sample(pressed, now);
  }
  for (Key expected : {Key::Back, Key::Ok, Key::Menu})
    check(keys.pop(event) && event.key == expected && event.press == Press::Short);
  check(!keys.pop(event) && keys.dropped() == 0);
  // Held repeat keys cannot starve an independent short MENU during a five-second stall.
  pressed = {};
  keys.begin(pressed, 0);
  for (uint32_t now = 0; now <= 5000; now += 5) {
    pressed[1] = now >= 40 && now < 4800;
    pressed[4] = now >= 3000 && now < 3090;
    keys.sample(pressed, now);
  }
  check(keys.pop(event) && event.key == Key::Prev && event.press == Press::Long);
  check(keys.pop(event) && event.key == Key::Prev && event.press == Press::Repeat);
  check(keys.pop(event) && event.key == Key::Menu && event.press == Press::Short);
  check(!keys.pop(event) && keys.dropped() == 0);
  // Preserve wake suppression and debounce, including millis wrap and simultaneous buttons.
  pressed.fill(true);
  keys.begin(pressed, 0xfffffe00U);
  keys.sample(pressed, 0xffffff00U);
  pressed.fill(false);
  keys.sample(pressed, 0xfffffff0U);
  keys.sample(pressed, 0x20U);
  check(!keys.pop(event));
  pressed.fill(true);
  keys.sample(pressed, 0x40U);
  keys.sample(pressed, 0x65U);
  pressed.fill(false);
  keys.sample(pressed, 0xa0U);
  keys.sample(pressed, 0xc0U);
  for (unsigned i = 0; i < 5; ++i)
    check(keys.pop(event) && event.key == static_cast<Key>(i) && event.press == Press::Short);
  check(!keys.pop(event));
  // A bounded queue reports saturation rather than silently losing discrete presses.
  keys.begin(pressed, 0);
  for (uint32_t now = 0; now <= 8000; now += 5) {
    pressed[2] = now % 160 < 80;
    keys.sample(pressed, now);
  }
  unsigned count = 0;
  while (keys.pop(event)) {
    check(event.key == Key::Ok && event.press == Press::Short);
    ++count;
  }
  check(count == 32 && keys.dropped() == 18);
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
  check(crc32("", 0) == 0);
  check(~crc32Update(crc32Update(0xFFFFFFFF, "1234", 4), "56789", 5) == 0xCBF43926);
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
// Keys one letter with the given dot/dash lengths and returns the keyer's code.
std::string keyLetter(morse::Keyer &k, const char *code, uint32_t &now, uint32_t dot, uint32_t dash) {
  for (const char *c = code; *c; ++c) {
    k.down(now);
    now += *c == '.' ? dot : dash;
    k.up(now);
    now += dot;
    check(!k.letterDue(now));
  }
  now += 2 * k.split();
  check(k.letterDue(now));
  std::string out = k.code();
  k.clear();
  return out;
}
std::string typed(morse::Editor &e) {
  char out[morse::Editor::capacity + 1];
  e.preview(out, sizeof(out));
  return out;
}
void keyJamo(morse::Editor &e, std::initializer_list<const char *> codes) {
  for (const char *c : codes)
    check(e.letter(c) == morse::Editor::Result::Added);
}
void morseTests() {
  // Timing mode: OK reports debounced edges with the time the level changed; others unchanged.
  InputEvents keys;
  std::array<bool, 5> pressed{};
  InputEvent event;
  keys.begin(pressed, 0);
  keys.timing(true);
  for (uint32_t t = 0; t <= 2000; t += 5) {
    pressed[2] = (t >= 100 && t < 160) || (t >= 400 && t < 1500);
    pressed[3] = t >= 1700 && t < 1800;
    keys.sample(pressed, t);
  }
  check(keys.pop(event) && event.key == Key::Ok && event.press == Press::Down && event.at == 100);
  check(keys.pop(event) && event.press == Press::Up && event.at == 160);
  check(keys.pop(event) && event.press == Press::Down && event.at == 400);
  check(keys.pop(event) && event.press == Press::Up && event.at == 1500);
  check(keys.pop(event) && event.key == Key::Next && event.press == Press::Short);
  check(!keys.pop(event));
  keys.timing(false);
  for (uint32_t t = 2000; t <= 2400; t += 5) {
    pressed[2] = t >= 2100 && t < 2200;
    keys.sample(pressed, t);
  }
  check(keys.pop(event) && event.key == Key::Ok && event.press == Press::Short && !keys.pop(event));
  // Practiced speed: 60 ms dots, 180 ms dashes; the split settles between them.
  morse::Keyer k;
  k.reset();
  uint32_t now = 1000;
  check(keyLetter(k, ".-..", now, 60, 180) == ".-..");
  check(k.split() > 60 && k.split() < 180);
  check(keyLetter(k, "--.-", now, 60, 180) == "--.-");
  // A slow beginner (280 / 900 ms) is read correctly once both lengths have appeared.
  k.reset();
  now = 0xFFFFF000U; // Across the millis wrap.
  keyLetter(k, "-.", now, 280, 900);
  check(k.split() > 280 && k.split() < 900);
  check(keyLetter(k, "..-.", now, 280, 900) == "..-.");
  // Too many symbols mark the letter as overflowed instead of growing the code.
  k.reset();
  for (int i = 0; i < 9; ++i) {
    k.down(i * 200);
    k.up(i * 200 + 60);
  }
  check(k.overflow() && strlen(k.code()) == morse::Keyer::maxCode);
  check(!strcmp(morse::latin("..--.."), "?") && !morse::latin("........"));
  check(morse::jamo(".-..") == 0x3131 && morse::jamo("-.--") == 0x3154);

  morse::Editor e;
  e.clear();
  // 안녕: ㅇㅏㄴ ㄴㅕㅇ — the second ㄴ starts a new syllable, ㅇ closes as a final.
  keyJamo(e, {"-.-", ".", "..-.", "..-.", "...", "-.-"});
  check(typed(e) == "안녕");
  // 각 + ㅏ moves the final: 가가. 닭 keeps its cluster until a vowel takes ㄱ: 달기.
  e.clear();
  keyJamo(e, {".-..", ".", ".-..", "."});
  check(typed(e) == "가가");
  e.clear();
  keyJamo(e, {"-...", ".", "...-", ".-..", "-.-"});
  check(typed(e) == "닭ㅇ");
  e.clear();
  keyJamo(e, {"-...", ".", "...-", ".-..", "..-"});
  check(typed(e) == "달기");
  // Doubled consonants and compound vowels: 꽈, 있어 (ㅆ final stays before ㅇ), 밖이 → 바끼.
  e.clear();
  keyJamo(e, {".-..", ".-..", ".-", "."});
  check(typed(e) == "꽈");
  e.clear();
  keyJamo(e, {"-.-", "..-", "--.", "--.", "-.-", "-"});
  check(typed(e) == "있어");
  e.clear();
  keyJamo(e, {".--", ".", ".-..", ".-..", "..-"});
  check(typed(e) == "바끼");
  // Sealing keeps 박 closed so the next ㄱ starts 기.
  e.clear();
  keyJamo(e, {".--", ".", ".-.."});
  e.seal();
  keyJamo(e, {".-..", "..-"});
  check(typed(e) == "박기");
  // Erase removes jamo of the open syllable first, then whole committed characters.
  e.erase();
  check(typed(e) == "박ㄱ");
  e.erase();
  check(typed(e) == "박");
  e.erase();
  check(typed(e).empty() && e.empty());
  e.erase(); // Erasing empty text is harmless.
  check(typed(e).empty());
  // Digits work in Hangul mode; letters need the Latin alphabet.
  check(e.letter(".----") == morse::Editor::Result::Added);
  check(e.letter("...---...") == morse::Editor::Result::Unknown);
  e.toggleAlphabet();
  check(!e.hangul());
  keyJamo(e, {"...", "---", "..."});
  check(e.space() == morse::Editor::Result::Added);
  check(e.letter(".-.-.-") == morse::Editor::Result::Added);
  check(std::string(e.finish()) == "1SOS .");
  // The buffer refuses input instead of overflowing.
  e.clear();
  e.toggleAlphabet();
  check(e.hangul());
  morse::Editor::Result r = morse::Editor::Result::Added;
  for (int i = 0; i < 2000 && r == morse::Editor::Result::Added; ++i)
    r = e.letter(i % 2 ? "." : "--");
  check(r == morse::Editor::Result::Full && e.bytes() <= morse::Editor::capacity);
  check(validUtf8(e.finish()));
  // Editing a saved memo: the cursor starts at the end and steps over whole characters.
  e.clear();
  e.load("가A나");
  check(e.atEnd() && typed(e) == "가A나");
  check(e.left() && e.left()); // Between 가 and A.
  check(!e.atEnd());
  size_t caret = 0;
  char out[morse::Editor::capacity + 1];
  // A syllable keyed mid-text composes at the cursor and shows there.
  keyJamo(e, {"-...", "."}); // 다
  e.preview(out, sizeof(out), &caret);
  check(std::string(out) == "가다A나" && caret == strlen("가다"));
  // Moving closes the open syllable, so a vowel after a move cannot steal its final.
  keyJamo(e, {"-.-"}); // 당 (open)
  check(e.composing());
  check(e.right()); // Past A.
  check(!e.composing());
  keyJamo(e, {"."});
  check(typed(e) == "가당Aㅏ나");
  e.erase();
  e.erase(); // Removes A, before the cursor.
  check(typed(e) == "가당나");
  e.preview(out, sizeof(out), &caret);
  check(caret == strlen("가당"));
  check(e.left() && e.left() && !e.left()); // Stops at the start.
  e.erase(); // Nothing before the cursor.
  check(typed(e) == "가당나");
  check(e.space() == morse::Editor::Result::Added);
  check(std::string(e.finish()) == " 가당나");
  check(e.right() && e.right() && e.right() && e.atEnd() && !e.right());
  // A newline from a file edited on a PC is one character to the cursor.
  e.load("a\nb");
  check(e.left() && e.left());
  e.erase();
  check(std::string(e.finish()) == "\nb");
  // Loading too much keeps whole characters only.
  std::string longText(morse::Editor::capacity - 1, 'x');
  longText += "가";
  e.load(longText.c_str());
  check(e.bytes() == morse::Editor::capacity - 1 && validUtf8(e.finish()));
}
int main() {
  textTests();
  logicTests();
  buttonTests();
  inputBacklogTests();
  morseTests();
  lyricTests();
  mediaTests();
  amsTests();
  std::cout << checks << " checks passed\n";
}

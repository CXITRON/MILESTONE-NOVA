#include "core/Logic.h"
#include "core/Text.h"
#include "media/AmsDecoder.h"
#include "media/Formats.h"
#include "media/Playback.h"
#include "settings/Values.h"
#include "storage/Journal.h"
#include "ui/Navigation.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>
#include <random>
#include <vector>
using namespace nova;
int main() {
  // Each threshold has its own recovery hysteresis. Invalid samples cannot
  // release an existing stop, and a sudden cooldown can safely skip levels.
  assert(thermalLevel(91, 0, 70, 80, 90) == 3);
  assert(thermalLevel(86, 3, 70, 80, 90) == 3);
  assert(thermalLevel(84, 3, 70, 80, 90) == 2);
  assert(thermalLevel(79, 2, 70, 80, 90) == 2);
  assert(thermalLevel(74, 2, 70, 80, 90) == 1);
  assert(thermalLevel(66, 1, 70, 80, 90) == 1);
  assert(thermalLevel(64, 3, 70, 80, 90) == 0);
  assert(thermalLevel(std::numeric_limits<float>::quiet_NaN(), 3, 70, 80, 90) == 3);
  assert(thermalLevel(std::numeric_limits<float>::quiet_NaN(), 0, 70, 80, 90) == 2);
  Navigation nav;
  nav.openMenu();
  nav.move(2);
  assert(nav.action() == MenuAction::Media);
  nav.back();
  assert(nav.profile() == Profile::Core && nav.screen() == Screen::Clock);
  nav.select(Profile::Now);
  nav.move(1);
  assert(nav.screen() == Screen::Now);
  nav.openMenu();
  nav.move(-1);
  assert(nav.action() == MenuAction::Media);
  nav.back();
  assert(nav.screen() == Screen::Now);
  nav.select(Profile::Media);
  nav.move(-1);
  assert(nav.screen() == Screen::Media);
  nav.select(Profile::Core);
  uint8_t order[]{8, 7, 6, 5, 4, 3, 2, 1, 0};
  nav.move(1, 1 << 8, order);
  assert(nav.screen() == Screen::Environment);
  nav.move(1, 1 << 8, order);
  assert(nav.core() == 8);
  nav.move(-1, 511, order);
  assert(nav.core() == 0);
  Playback p;
  p.load(10000, 0xfffffff0U);
  p.toggle(0xfffffff0U);
  assert(p.position(0x54) == 100);
  p.seek(5000, false, 100);
  assert(p.position(1000) == 5000);
  p.toggle(1000);
  p.tick(7100, true);
  assert(p.position(7100) == 1100);
  p.startSync(27, 20000, 0);
  assert(!p.sync(28, 1, 0, true, 0));
  assert(p.sync(27, 1, 2000, true, 100));
  assert(!p.sync(27, 1, 0, false, 200));
  p.tick(2601, true);
  assert(!p.playing() && p.stale() && p.position(5000) == 4500);
  assert(p.sync(27, 2, 7000, true, 6000));
  assert(p.playing() && !p.stale());
  p.stopSync(6100);
  assert(!p.sync(27, 3, 0, true, 7000));
  p.startSync(1, 10000, 0);
  assert(p.sync(1, 0xffffffffU, 5000, true, 0));
  assert(p.sync(1, 0, 2000, false, 10));
  assert(p.position(100) == 2000);
  assert(safeStoragePath("/media/한글.nvi"));
  for (const char *s : {"/media/../config/key", "/media//test", "/other/x", "media/x", "/media/./x",
                        "/media/xx\\yy"})
    assert(!safeStoragePath(s));
  std::vector<uint8_t> image(51216);
  memcpy(image.data(), "NVI1", 4);
  write16(image.data() + 4, 160);
  write16(image.data() + 6, 160);
  write32(image.data() + 8, 51200);
  write32(image.data() + 12, crc32(image.data() + 16, 51200));
  MediaInfo info;
  assert(mediaHeader(image.data(), 16, image.size(), info) && info.format == MediaFormat::Image &&
         info.frames == 1);
  assert(!mediaHeader(image.data(), 16, image.size() - 1, info));
  // NVI1/NJV1 accept only the NOVA square sides; payload length must match the side.
  for (unsigned side : {200U, 240U}) {
    write16(image.data() + 4, side);
    write16(image.data() + 6, side);
    write32(image.data() + 8, rawBytes(side));
    assert(mediaHeader(image.data(), 16, 16 + rawBytes(side), info) && info.width == side);
    assert(!mediaHeader(image.data(), 16, 51216, info));
  }
  write16(image.data() + 6, 200);
  assert(!mediaHeader(image.data(), 16, 16 + rawBytes(240), info)); // Not square.
  write16(image.data() + 4, 128);
  write16(image.data() + 6, 128);
  write32(image.data() + 8, rawBytes(128));
  assert(!mediaHeader(image.data(), 16, 16 + rawBytes(128), info));
  std::vector<uint8_t> jpegVideo(16);
  memcpy(jpegVideo.data(), "NJV1", 4);
  write16(jpegVideo.data() + 8, 20);
  write32(jpegVideo.data() + 12, 10);
  for (unsigned side : {160U, 240U}) {
    write16(jpegVideo.data() + 4, side);
    write16(jpegVideo.data() + 6, side);
    assert(mediaHeader(jpegVideo.data(), 16, 4096, info) && info.width == side);
  }
  write16(jpegVideo.data() + 4, 320);
  write16(jpegVideo.data() + 6, 320);
  assert(!mediaHeader(jpegVideo.data(), 16, 4096, info));
  // Smooth resize keeps solid colors exact and averages when reducing.
  std::vector<uint16_t> solid(88 * 88, 0x07e0), large(rawBytes(200) / 2);
  resample565(solid.data(), 88, 88, large.data(), 200);
  assert(std::all_of(large.begin(), large.end(), [](uint16_t p) { return p == 0x07e0; }));
  const uint16_t checker[]{0xffff, 0x0000, 0x0000, 0xffff};
  uint16_t reduced = 0;
  resample565(checker, 2, 2, &reduced, 1);
  assert(reduced == 0x7bef); // Mid gray, not one sampled corner.
  std::vector<uint8_t> video(16);
  memcpy(video.data(), "MVJ1", 4);
  write16(video.data() + 4, 128);
  write16(video.data() + 6, 128);
  write16(video.data() + 8, 20);
  write32(video.data() + 12, 1200);
  assert(mediaHeader(video.data(), 16, 20000, info) && info.duration == 60000);
  write16(video.data() + 8, 31);
  assert(!mediaHeader(video.data(), 16, 20000, info));
  std::vector<uint8_t> frame(8), delta{0, 20, 0, 8, 0, 1, 2, 3, 4, 5, 6, 7, 8};
  assert(deltaFrame(delta.data(), delta.size(), frame.data(), 8, true));
  const uint8_t x[]{1, 20, 0, 3, 0, 0x80, 0xff, 6};
  assert(deltaFrame(x, sizeof(x), frame.data(), 8, false));
  assert(frame[0] == 0xfe && frame[7] == 8);
  assert(!deltaFrame(x, sizeof(x), frame.data(), 8, true));
  std::vector<uint8_t> art(22704);
  memcpy(art.data(), "MAC1", 4);
  art[4] = art[5] = 60;
  art[6] = art[7] = 88;
  art[8] = 28;
  art[9] = 32;
  art[10] = 60;
  art[11] = 128;
  art[7216] = 0xf8;
  art[7217] = 0;
  const uint32_t crc = crc32(art.data() + 16, art.size() - 16);
  for (unsigned i = 0; i < 4; i++)
    art[12 + i] = crc >> (24 - i * 8);
  uint16_t pixels[160 * 160]{};
  assert(artworkPacket(art.data(), art.size(), pixels, 160) && pixels[0] == 0xf800);
  art.back() ^= 1;
  assert(!artworkPacket(art.data(), art.size(), pixels, 160));
  uint8_t journal[logJournalMaximum];
  const uint8_t line[]{'a', ',', 'b', '\n'};
  assert(
      encodeLogJournal(journal, sizeof(journal), "/logs/2026-09-24.csv", 80, line, sizeof(line)));
  assert(validateLogJournal(journal, logJournalHeader + sizeof(line)));
  journal[132] ^= 1;
  assert(!validateLogJournal(journal, logJournalHeader + sizeof(line)));
  assert(!encodeLogJournal(journal, sizeof(journal), "/media/x", 0, line, sizeof(line)));
  Secrets saved;
  WifiProfile wifi;
  for (unsigned i = 0; i < 10; ++i) {
    snprintf(wifi.ssid, sizeof(wifi.ssid), "Network %u", i);
    assert(rememberWifiProfile(saved, wifi));
  }
  assert(saved.networkCount == 8 && !strcmp(saved.networks[0].ssid, "Network 9"));
  assert(!strcmp(saved.networks[7].ssid, "Network 2"));
  strcpy(wifi.ssid, "Network 5");
  assert(rememberWifiProfile(saved, wifi));
  assert(saved.networkCount == 8 && !strcmp(saved.networks[0].ssid, "Network 5"));
  assert(!strcmp(saved.networks[1].ssid, "Network 9"));
  wifi.auth = 2;
  assert(!rememberWifiProfile(saved, wifi));
  Settings settings;
  assert(applySetting(settings, "now_layout", "5") && settings.nowLayout == 5);
  assert(validSettings(settings));
  assert(!applySetting(settings, "now_layout", "6") && settings.nowLayout == 5);
  assert(!applySetting(settings, "now_layout", "-1") && settings.nowLayout == 5);
  assert(applySetting(settings, "core_order", "8,7,6,5,4,3,2,1,0"));
  assert(!applySetting(settings, "core_order", "8,7,6,5,4,3,2,1,1"));
  assert(!applySetting(settings, "thermal_stop", "60"));
  assert(!applySetting(settings, "temperature_offset", "nan"));
  assert(!applySetting(settings, "message", std::string(200, 'x').c_str()));
  auto *raw = reinterpret_cast<uint8_t *>(&settings);
  raw[offsetof(Settings, hour24)] = 2;
  assert(!validSettings(settings));
  MediaSession session;
  AmsDecoder decoder;
  const auto send = [&](uint8_t a, uint8_t b, const char *value) {
    std::vector<uint8_t> data{a, b, 0};
    data.insert(data.end(), value, value + strlen(value));
    decoder.accept(data.data(), data.size(), 0, session);
  };
  send(0, 0, "Apple Music");
  send(0, 2, "0.625");
  send(1, 0, "3");
  send(1, 1, "12");
  send(1, 2, "2");
  assert(!strcmp(session.player().name, "Apple Music") && session.player().volume == .625f &&
         session.player().queueIndex == 3 && session.player().queueCount == 12 &&
         session.player().shuffle == 2);
  send(1, 1, "-1");
  assert(session.player().queueCount == 12);
  send(0, 2, "1.5");
  assert(session.player().volume == .625f);
  std::mt19937 rng(4567);
  std::vector<uint8_t> fuzz(4096), out(2048);
  for (unsigned n = 0; n < 30000; ++n) {
    const size_t size = rng() % fuzz.size();
    for (size_t i = 0; i < size; ++i)
      fuzz[i] = rng();
    mediaHeader(fuzz.data(), size, rng(), info);
    deltaFrame(fuzz.data(), size, out.data(), out.size(), rng() & 1);
    validateLogJournal(fuzz.data(), size);
    artworkPacket(fuzz.data(), size, nullptr, 0);
  }
  std::cout
      << "Parity navigation, playback, formats, settings, AMS and 30000 malformed inputs passed\n";
}

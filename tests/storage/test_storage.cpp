#include "artwork/Artwork.h"
#include "core/Text.h"
#include "lyrics/OnlineLyrics.h"
#include "media/Session.h"
#include "network/Http.h"
#include "storage/Journal.h"
#include "storage/Storage.h"
#include <cassert>
#include <cstring>
#include <esp_timer.h>
#include <fstream>
#include <functional>
#include <iostream>
#include <jpeglib.h>
TestSD SD;
std::vector<uint8_t> httpPacket;
size_t httpAt = 0;
unsigned httpRequests = 0;
std::string httpForm;
size_t httpReadChunk = SIZE_MAX;
int64_t httpReadUs = 0;
int httpStatus = 200;
unsigned httpTimeout = 0;
int64_t httpDeclaredLength = -2;
bool httpComplete = true;
std::function<void()> httpBeforeRead;
namespace nova {
void log(const char *, const char *, ...) {}
struct StorageTestAccess {
  static void init(Storage &s) {
    s.buffer_ = new AssetResult;
    s.catalog_ = new MediaCatalog;
    s.published_ = new MediaCatalog;
    assert(s.decoder_.begin());
    s.mounted_ = true;
  }
  static bool run(Storage &s, FileJob &j) {
    s.fileJob(j);
    return j.ok;
  }
  static void recover(Storage &s) { s.recoverTransfer(); }
  static bool journal(Storage &s) { return s.recoverLog(); }
  static MediaCatalog catalog(Storage &s) { return *s.published_; }
  static MediaDecoder &decoder(Storage &s) { return s.decoder_; }
  static void mounted(Storage &s, bool value) { s.mounted_ = value; }
};
// These virtual/unused members are not part of the tested worker operation surface.
bool Storage::request(const char *, uint32_t) { return false; }
// File handler semantics run synchronously here; RTOS scheduling is not emulated.
bool Storage::execute(FileJob &j) {
  fileJob(j);
  return j.ok;
}
Http::~Http() = default;
bool Http::open(const char *, const char *form, unsigned timeoutMs) {
  ++httpRequests;
  httpForm = form ? form : "";
  httpAt = 0;
  length_ = httpDeclaredLength == -2 ? int64_t(httpPacket.size()) : httpDeclaredLength;
  status_ = httpStatus;
  httpTimeout = timeoutMs;
  return status_ == 200;
}
int Http::read(uint8_t *bytes, size_t n) {
  if (httpBeforeRead) {
    auto callback = std::move(httpBeforeRead);
    callback();
  }
  n = std::min(n, httpPacket.size() - httpAt);
  n = std::min(n, httpReadChunk);
  testTimeUs += httpReadUs;
  memcpy(bytes, httpPacket.data() + httpAt, n);
  httpAt += n;
  return n;
}
bool Http::complete() const { return httpComplete && httpAt == httpPacket.size(); }
} // namespace nova
using namespace nova;
std::vector<uint8_t> read(const char *path) {
  std::ifstream f(SD.root + path, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), {}};
}
void write(const char *path, const std::vector<uint8_t> &bytes) {
  std::ofstream f(SD.root + path, std::ios::binary);
  f.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}
std::vector<uint8_t> image(uint16_t color) {
  std::vector<uint8_t> bytes(51216);
  memcpy(bytes.data(), "NVI1", 4);
  write16(bytes.data() + 4, 160);
  write16(bytes.data() + 6, 160);
  write32(bytes.data() + 8, 51200);
  for (unsigned i = 16; i < bytes.size(); i += 2)
    write16(bytes.data() + i, color);
  write32(bytes.data() + 12, crc32(bytes.data() + 16, 51200));
  return bytes;
}
std::vector<uint8_t> jpeg(unsigned side, uint8_t red) {
  jpeg_compress_struct c{};
  jpeg_error_mgr e{};
  c.err = jpeg_std_error(&e);
  jpeg_create_compress(&c);
  unsigned char *data = nullptr;
  unsigned long n = 0;
  jpeg_mem_dest(&c, &data, &n);
  c.image_width = c.image_height = side;
  c.input_components = 3;
  c.in_color_space = JCS_RGB;
  jpeg_set_defaults(&c);
  jpeg_start_compress(&c, TRUE);
  std::vector<uint8_t> row(side * 3);
  for (unsigned x = 0; x < side; x++)
    row[x * 3] = red;
  while (c.next_scanline < c.image_height) {
    JSAMPROW p = row.data();
    jpeg_write_scanlines(&c, &p, 1);
  }
  jpeg_finish_compress(&c);
  std::vector<uint8_t> out(data, data + n);
  free(data);
  jpeg_destroy_compress(&c);
  return out;
}
std::vector<uint8_t> video(const char *magic, unsigned side) {
  std::vector<uint8_t> out(16);
  memcpy(out.data(), magic, 4);
  write16(out.data() + 4, side);
  write16(out.data() + 6, side);
  write16(out.data() + 8, 10);
  write32(out.data() + 12, 2);
  for (uint8_t red : {uint8_t(255), uint8_t(0)}) {
    auto frame = jpeg(side, red);
    size_t at = out.size();
    out.resize(at + 8 + frame.size());
    write32(out.data() + at, frame.size());
    write32(out.data() + at + 4, crc32(frame.data(), frame.size()));
    memcpy(out.data() + at + 8, frame.data(), frame.size());
  }
  return out;
}
std::vector<uint8_t> movie(bool color) {
  const size_t raw = color ? 16384 : 2048;
  std::vector<uint8_t> out(24 + 5 + raw);
  memcpy(out.data(), "MSM1", 4);
  out[4] = 1;
  out[5] = color ? 0 : 3;
  out[6] = out[7] = 128;
  write16(out.data() + 8, color ? 1 : 2);
  write16(out.data() + 10, color);
  write32(out.data() + 12, color ? 0 : 200);
  write16(out.data() + 25, color ? 0 : 100);
  write16(out.data() + 27, raw);
  if (color)
    std::fill(out.begin() + 29, out.end(), 0xe0); // RGB332 red
  else {
    const size_t at = out.size();
    out.resize(at + 23);
    out[at] = 1;
    write16(out.data() + at + 1, 100);
    write16(out.data() + at + 3, 18);
    out[at + 5] = 0x80;
    out[at + 6] = 0xff;
    std::fill(out.begin() + at + 7, out.end() - 1, 127);
    out.back() = 126;
  }
  write32(out.data() + 16, out.size() - 24);
  write32(out.data() + 20, crc32(out.data() + 24, out.size() - 24));
  return out;
}
int main(int argc, char **argv) {
  assert(argc == 2);
  SD.root = argv[1];
  std::filesystem::create_directories(SD.root);
  for (const char *dir : {"/media", "/artwork", "/logs", "/update", "/lyrics"})
    SD.mkdir(dir);
  Storage s;
  StorageTestAccess::init(s);
  FileJob job;
  job.op = FileOp::Scan;
  assert(StorageTestAccess::run(s, job));
  auto original = image(0xf800), replacement = image(0x07e0);
  write("/media/a.nvi", original);
  job = {};
  job.op = FileOp::UploadBegin;
  strcpy(job.path, "/media/a.nvi");
  job.id = 42;
  job.total = replacement.size();
  assert(StorageTestAccess::run(s, job));
  job.op = FileOp::UploadChunk;
  job.offset = 0;
  job.data = replacement.data();
  job.length = 12000;
  job.checksum = crc32(job.data, job.length);
  assert(StorageTestAccess::run(s, job) && job.offset == 12000);
  job.offset = 0;
  assert(StorageTestAccess::run(s, job) && job.offset == 12000);
  replacement[200] ^= 1;
  job.offset = 0;
  job.checksum = crc32(job.data, job.length);
  assert(!StorageTestAccess::run(s, job));
  replacement[200] ^= 1;
  // Reboot after durable first chunk recovers exactly that checkpoint.
  Storage resumed;
  StorageTestAccess::init(resumed);
  StorageTestAccess::recover(resumed);
  job = {};
  job.op = FileOp::UploadStatus;
  assert(StorageTestAccess::run(resumed, job) && job.offset == 12000 && job.id == 42);
  job.op = FileOp::UploadChunk;
  job.offset = 12000;
  job.data = replacement.data() + 12000;
  job.length = replacement.size() - 12000;
  job.checksum = crc32(job.data, job.length);
  assert(StorageTestAccess::run(resumed, job));
  // Atomic rename failure restores the previous playable item.
  job.op = FileOp::UploadCommit;
  failRenameSource = "/media/a.nvi.part";
  assert(!StorageTestAccess::run(resumed, job));
  assert(read("/media/a.nvi") == original);
  assert(StorageTestAccess::run(resumed, job));
  assert(read("/media/a.nvi") == replacement);
  // A written chunk is not acknowledged until the checkpoint is durable.
  job = {};
  job.op = FileOp::UploadBegin;
  strcpy(job.path, "/media/checkpoint.nvi");
  job.id = 44;
  job.total = replacement.size();
  assert(StorageTestAccess::run(resumed, job));
  job.op = FileOp::UploadChunk;
  job.data = replacement.data();
  job.length = 8192;
  job.checksum = crc32(job.data, job.length);
  failWriteAfter = 1; // Data write succeeds; checkpoint temp write fails.
  assert(!StorageTestAccess::run(resumed, job) && job.offset == 0);
  failWriteAfter = -1;
  assert(StorageTestAccess::run(resumed, job) && job.offset == 8192);
  job.op = FileOp::UploadAbort;
  assert(StorageTestAccess::run(resumed, job));
  assert(!SD.exists("/media/.transfer"));
  auto catalog = StorageTestAccess::catalog(resumed);
  assert(catalog.count == 1);
  job = {};
  job.op = FileOp::MediaEdit;
  job.id = 0;
  job.offset = 0;
  job.value = 17;
  job.enabled = false;
  strcpy(job.text, "사용자 이름");
  assert(StorageTestAccess::run(resumed, job));
  job.op = FileOp::Scan;
  assert(StorageTestAccess::run(resumed, job));
  catalog = StorageTestAccess::catalog(resumed);
  assert(!catalog.entries[0].enabled && catalog.entries[0].seconds == 17 &&
         !strcmp(catalog.entries[0].title, "사용자 이름"));
  // Full-file corruption must fail commit without replacing the old image.
  replacement.back() ^= 1;
  job = {};
  job.op = FileOp::UploadBegin;
  strcpy(job.path, "/media/a.nvi");
  job.id = 43;
  job.total = replacement.size();
  assert(StorageTestAccess::run(resumed, job));
  job.op = FileOp::UploadChunk;
  job.data = replacement.data();
  job.length = replacement.size();
  job.checksum = crc32(job.data, job.length);
  assert(StorageTestAccess::run(resumed, job));
  job.op = FileOp::UploadCommit;
  assert(!StorageTestAccess::run(resumed, job));
  replacement.back() ^= 1;
  assert(read("/media/a.nvi") == replacement);
  // Validate legacy 128px, first NOVA 160px and full-width 240px JPEG sequences and indexed seek.
  auto &decoder = StorageTestAccess::decoder(resumed);
  std::atomic<uint32_t> progress{0};
  std::atomic<bool> cancel{false};
  constexpr unsigned side = board::mediaSide;
  static uint16_t pixels[side * side];
  uint32_t duration = 0;
  for (const auto &entry :
       {std::pair<const char *, unsigned>{"MVJ1", 128}, {"NJV1", 160}, {"NJV1", 240}}) {
    auto bytes = video(entry.first, entry.second);
    write("/media/test.njv", bytes);
    uint32_t contentCrc = 0;
    assert(decoder.validate("/media/test.njv", progress, cancel, &contentCrc));
    assert(contentCrc == crc32(bytes.data(), bytes.size()));
    assert(decoder.frame("/media/test.njv", 0, pixels, duration, side) && duration == 200 &&
           (pixels[0] & 0xf800) > 0xd000 && (pixels[side * side - 1] & 0xf800) > 0xd000);
    assert(decoder.frame("/media/test.njv", 100, pixels, duration, side) && pixels[0] == 0);
    decoder.close();
    bytes.back() ^= 1;
    write("/media/test.njv", bytes);
    assert(!decoder.validate("/media/test.njv", progress, cancel));
  }
  for (bool color : {false, true}) {
    auto bytes = movie(color);
    write("/media/old.msm", bytes);
    uint32_t contentCrc = 0;
    assert(decoder.validate("/media/old.msm", progress, cancel, &contentCrc));
    assert(contentCrc == crc32(bytes.data(), bytes.size()));
    assert(decoder.frame("/media/old.msm", 0, pixels, duration, side));
    assert(pixels[0] == (color ? 0xf800 : 0));
    if (!color) {
      assert(decoder.frame("/media/old.msm", 150, pixels, duration, side) && pixels[0] == 0xffff);
      assert(decoder.frame("/media/old.msm", 0, pixels, duration, side) && pixels[0] == 0);
    }
    decoder.close();
  }
  std::vector<uint8_t> bitmap(54 + 8);
  bitmap[0] = 'B';
  bitmap[1] = 'M';
  write32(bitmap.data() + 10, 54);
  write32(bitmap.data() + 14, 40);
  write32(bitmap.data() + 18, 1);
  write16(bitmap.data() + 26, 1);
  write16(bitmap.data() + 28, 24);
  bitmap[56] = 255; // First stored row red; second row blue, four-byte aligned.
  bitmap[58] = 255;
  for (int height : {2, -2}) {
    write32(bitmap.data() + 22, height);
    write("/media/old.bmp", bitmap);
    uint32_t contentCrc = 0;
    assert(decoder.validate("/media/old.bmp", progress, cancel, &contentCrc));
    assert(contentCrc == crc32(bitmap.data(), bitmap.size()));
    assert(decoder.frame("/media/old.bmp", 0, pixels, duration, side));
    assert(pixels[0] == (height > 0 ? 0x001f : 0xf800));
    assert(pixels[(side - 1) * side] == (height > 0 ? 0xf800 : 0x001f));
    decoder.close();
  }
  cancel = true;
  write("/media/cancel.njv", video("NJV1", 160));
  assert(!decoder.validate("/media/cancel.njv", progress, cancel));
  assert(!SD.exists("/media/cancel.njv.nix.tmp"));
  cancel = false;
  // Full-width 240 NVI is copied exactly; first-generation 160 NVI is enlarged smoothly.
  std::vector<uint8_t> wide(16 + rawBytes(side));
  memcpy(wide.data(), "NVI1", 4);
  write16(wide.data() + 4, side);
  write16(wide.data() + 6, side);
  write32(wide.data() + 8, rawBytes(side));
  for (unsigned i = 0; i < side * side; ++i)
    write16(wide.data() + 16 + i * 2, uint16_t(i));
  write32(wide.data() + 12, crc32(wide.data() + 16, rawBytes(side)));
  write("/media/wide.nvi", wide);
  assert(decoder.validate("/media/wide.nvi", progress, cancel));
  assert(decoder.frame("/media/wide.nvi", 0, pixels, duration, side) && pixels[1234] == 1234);
  write("/media/old.nvi", original);
  assert(decoder.frame("/media/old.nvi", 0, pixels, duration, side) && pixels[0] == 0xf800 &&
         pixels[side * side - 1] == 0xf800);
  decoder.close();
  std::vector<uint8_t> raw(16 + 4 + 51200);
  memcpy(raw.data(), "NVV1", 4);
  write16(raw.data() + 4, 160);
  write16(raw.data() + 6, 160);
  write16(raw.data() + 8, 10);
  write32(raw.data() + 12, 1);
  write32(raw.data() + 16, read32(original.data() + 12));
  memcpy(raw.data() + 20, original.data() + 16, 51200);
  for (const auto &bytes : {original, raw}) {
    write("/media/content-check.nvi", bytes);
    uint32_t contentCrc = 0;
    assert(decoder.validate("/media/content-check.nvi", progress, cancel, &contentCrc));
    assert(contentCrc == crc32(bytes.data(), bytes.size()));
  }
  // A valid old SD video must not be accepted for a different newly prepared source.
  auto syncBytes = video("NJV1", 160);
  write("/media/sync.njv", syncBytes);
  job = {};
  job.op = FileOp::SyncValidate;
  strcpy(job.path, "/media/sync.njv");
  job.offset = syncBytes.size();
  job.checksum = crc32(syncBytes.data(), syncBytes.size());
  assert(StorageTestAccess::run(resumed, job) && job.total == 200);
  ++job.offset;
  assert(!StorageTestAccess::run(resumed, job));
  --job.offset;
  job.checksum ^= 1;
  assert(!StorageTestAccess::run(resumed, job));
  // Header timing changes preserve all frame CRCs but must change the complete-file identity.
  write16(syncBytes.data() + 8, 20);
  write("/media/sync.njv", syncBytes);
  job.checksum ^= 1;
  assert(!StorageTestAccess::run(resumed, job));
  job.checksum = crc32(syncBytes.data(), syncBytes.size());
  assert(StorageTestAccess::run(resumed, job) && job.total == 100);
  write("/media/sync.njv", original);
  job.offset = original.size();
  job.checksum = crc32(original.data(), original.size());
  assert(!StorageTestAccess::run(resumed, job)); // A still is not a Sync video.
  // Metadata/search pagination and explicit delete preserve the MISSING state.
  Track track;
  strcpy(track.title, "곡 검색");
  strcpy(track.artist, "NOVA");
  for (unsigned i = 0; i < 35; ++i) {
    snprintf(track.key, sizeof(track.key), "%016x", i);
    job = {};
    job.op = FileOp::ArtRemember;
    job.data = reinterpret_cast<uint8_t *>(&track);
    job.length = sizeof(track);
    assert(StorageTestAccess::run(resumed, job));
    char name[128];
    snprintf(name, sizeof(name), "/artwork/%s.nvi", track.key);
    write(name, original);
  }
  FileListing listing;
  job = {};
  job.op = FileOp::List;
  strcpy(job.path, "/artwork");
  strcpy(job.text, "곡 검색");
  job.data = reinterpret_cast<uint8_t *>(&listing);
  job.length = sizeof(listing);
  assert(StorageTestAccess::run(resumed, job) && listing.count == 32);
  job.offset = 32;
  assert(StorageTestAccess::run(resumed, job) && listing.count == 3);
  job = {};
  job.op = FileOp::ArtFlags;
  strcpy(job.path, "/artwork/0000000000000000.nvi");
  job.value = 1;
  job.enabled = true;
  assert(StorageTestAccess::run(resumed, job));
  job.op = FileOp::ArtRefresh;
  job.data = reinterpret_cast<uint8_t *>(&track);
  job.length = sizeof(track);
  assert(!StorageTestAccess::run(resumed, job));
  job.op = FileOp::ArtCleanup;
  job.total = 0;
  assert(StorageTestAccess::run(resumed, job) && job.value == 1);
  assert(SD.exists("/artwork/0000000000000000.nvi"));
  job.op = FileOp::Remove;
  assert(StorageTestAccess::run(resumed, job));
  assert(SD.exists("/artwork/0000000000000000.missing"));
  job.op = FileOp::ArtRefresh;
  assert(StorageTestAccess::run(resumed, job));
  assert(!SD.exists("/artwork/0000000000000000.missing"));
  assert(!strcmp(track.title, "곡 검색"));
  write("/artwork/0000000000000000.nvi", original);
  assert(StorageTestAccess::run(resumed, job));
  assert(read("/artwork/0000000000000000.nvi") == original);
  // A complete append before power failure must not be duplicated during recovery.
  std::vector<uint8_t> record(logJournalHeader + 4);
  const uint8_t line[]{'a', ',', 'b', '\n'};
  assert(encodeLogJournal(record.data(), record.size(), "/logs/day.csv", 0, line, 4));
  write("/logs/pending", record);
  write("/logs/day.csv", std::vector<uint8_t>(line, line + 4));
  assert(StorageTestAccess::journal(resumed));
  assert(read("/logs/day.csv").size() == 4);
  // A partial append is repaired at its original offset.
  assert(encodeLogJournal(record.data(), record.size(), "/logs/day.csv", 4, line, 4));
  write("/logs/pending", record);
  write("/logs/day.csv", std::vector<uint8_t>{'a', ',', 'b', '\n', 'a'});
  assert(StorageTestAccess::journal(resumed));
  assert(read("/logs/day.csv").size() == 8);
  job = {};
  job.op = FileOp::Write;
  strcpy(job.path, "/lyrics/test.lrc");
  job.data = const_cast<uint8_t *>(line);
  job.length = 4;
  const auto beforeLyrics = resumed.assetRevision();
  failWriteAfter = 0;
  assert(!StorageTestAccess::run(resumed, job));
  assert(resumed.assetRevision() == beforeLyrics);
  failWriteAfter = -1;
  assert(StorageTestAccess::run(resumed, job));
  assert(resumed.assetRevision() == beforeLyrics + 1);
  assert(read("/lyrics/test.lrc").size() == 4);
  job = {};
  job.op = FileOp::MediaDelete;
  job.id = 0;
  assert(StorageTestAccess::run(resumed, job));
  assert(!SD.exists("/media/a.nvi"));

  // The real Artwork service with a deterministic MAC1 transport and real SD handlers.
  httpPacket.assign(22704, 0);
  memcpy(httpPacket.data(), "MAC1", 4);
  const uint8_t shape[]{60, 60, 88, 88, 28, 32, 60, 128};
  memcpy(httpPacket.data() + 4, shape, sizeof(shape));
  for (size_t at = 16; at < httpPacket.size(); at += 2) {
    httpPacket[at] = 0x07;
    httpPacket[at + 1] = 0xe0;
  }
  const uint32_t packetCrc = crc32(httpPacket.data() + 16, httpPacket.size() - 16);
  for (unsigned i = 0; i < 4; ++i)
    httpPacket[12 + i] = packetCrc >> (24 - i * 8);
  Artwork artwork;
  assert(artwork.begin(resumed));
  Track tooLong;
  for (char *field : {tooLong.title, tooLong.artist, tooLong.album}) {
    for (unsigned i = 0; i < 240; i += 2) {
      field[i] = char(0xc3);
      field[i + 1] = char(0xa9);
    }
  }
  assert(!artwork.fetch(tooLong, pixels) && httpRequests == 0);
  Track limitQuery;
  memset(limitQuery.title, '!', 240);
  memset(limitQuery.artist, '!', 219);
  strcpy(limitQuery.album, "AA");
  assert(artwork.fetch(limitQuery, pixels) && httpForm.size() == 1400);
  strcpy(limitQuery.album, "AAA");
  assert(!artwork.fetch(limitQuery, pixels) && httpRequests == 1);
  // The MAC1 answer above (gateway before its update) is enlarged smoothly to the cover side.
  assert(pixels[0] == 0x07e0 && pixels[board::artSide * board::artSide - 1] == 0x07e0);
  // The updated gateway answers with a baseline JPEG at the cover side; other sizes are rejected.
  const auto mac1 = httpPacket;
  Track jpegQuery;
  strcpy(jpegQuery.title, "Title");
  strcpy(jpegQuery.artist, "Artist");
  httpPacket = jpeg(board::artSide, 255);
  assert(artwork.fetch(jpegQuery, pixels));
  assert((pixels[0] & 0xf800) > 0xd000 &&
         (pixels[board::artSide * board::artSide - 1] & 0xf800) > 0xd000);
  httpPacket = jpeg(160, 255);
  assert(!artwork.fetch(jpegQuery, pixels));
  httpPacket.assign(65537, 0); // Over the JPEG bound.
  assert(!artwork.fetch(jpegQuery, pixels));
  httpPacket = mac1;
  httpRequests = 0;
  strcpy(track.key, "ffffffffffffffff");
  Settings settings;
  assert(artwork.request(track, 70, settings));
  artwork.process();
  assert(httpRequests == 1);
  std::fill(std::begin(pixels), std::end(pixels), 0xf800);
  bool artworkOk = false;
  assert(!artwork.receive(pixels, 71, artworkOk));
  assert(pixels[0] == 0xf800 && pixels[board::artSide * board::artSide - 1] == 0xf800);
  assert(!artwork.receive(pixels, 70, artworkOk)); // Stale completion was consumed.
  assert(artwork.request(track, 71, settings));
  artwork.process();
  assert(artwork.receive(pixels, 71, artworkOk) && artworkOk && pixels[0] == 0x07e0);
  const char *artPath = "/artwork/ffffffffffffffff.nvi";
  for (unsigned flag : {0U, 1U, 2U}) {
    job = {};
    job.op = FileOp::ArtFlags;
    strcpy(job.path, artPath);
    job.value = flag;
    job.enabled = true;
    const auto revision = resumed.assetRevision();
    assert(StorageTestAccess::run(resumed, job));
    assert(resumed.assetRevision() == revision + 1);
    const auto kept = read(artPath);
    const auto count = httpRequests;
    assert(artwork.request(track, 72 + flag, settings));
    artwork.process();
    assert(httpRequests == count); // No automatic request for custom/pin/block.
    assert(!artwork.save(track.key, pixels, false, settings));
    assert(read(artPath) == kept); // A late save also rechecks protection.
    job.enabled = false;
    assert(StorageTestAccess::run(resumed, job));
  }
  // A request queued before a portal custom upload must not overwrite that upload later.
  assert(artwork.request(track, 80, settings));
  std::fill(std::begin(pixels), std::end(pixels), 0xf800);
  assert(artwork.save(track.key, pixels, true, settings));
  const auto kept = read(artPath);
  const auto count = httpRequests;
  artwork.process();
  assert(httpRequests == count && read(artPath) == kept);
  job = {};
  job.op = FileOp::Remove;
  strcpy(job.path, artPath);
  assert(StorageTestAccess::run(resumed, job));
  assert(artwork.request(track, 81, settings));
  artwork.process();
  assert(httpRequests == count && !SD.exists(artPath)); // MISSING also blocks the request.
  httpReadChunk = 1;
  httpReadUs = 4000000;
  assert(!artwork.fetch(track, pixels));
  assert(httpAt == 4); // Deadline bounds a peer that keeps returning small chunks.
  httpReadChunk = SIZE_MAX;
  httpReadUs = 0;

  // Online lyrics: the gateway LRC is cached on SD and parsed for the requesting generation.
  OnlineLyrics online;
  assert(online.begin(resumed));
  Track song;
  strcpy(song.key, "abcdefabcdef0123");
  strcpy(song.title, "곡 & Title");
  strcpy(song.artist, "Artist");
  song.durationMs = 231400;
  const std::string lrc = "[00:01.00]첫 줄\n[00:04.50]Second line\n";
  httpPacket.assign(lrc.begin(), lrc.end());
  httpRequests = 0;
  const auto lyricsRevision = resumed.assetRevision();
  assert(online.request(song, 90));
  online.process();
  assert(httpRequests == 1 && httpForm.find("&album=&duration=231") != std::string::npos &&
         httpForm.find("%EA%B3%A1%20%26%20Title") != std::string::npos);
  const char *lrcPath = "/lyrics/abcdefabcdef0123.lrc";
  {
    const auto cached = read(lrcPath);
    assert(std::string(cached.begin(), cached.end()) == lrc);
  }
  assert(resumed.assetRevision() == lyricsRevision + 1); // App reloads the track from SD.
  Lyrics parsed;
  bool lyricsFound = false;
  assert(!online.receive(parsed, 91, lyricsFound)); // Stale generation is consumed unparsed.
  assert(parsed.count == 0);
  assert(online.request(song, 92));
  online.process(); // The cached file now exists, so no second lookup is made.
  assert(httpRequests == 1);
  assert(online.receive(parsed, 92, lyricsFound) && !lyricsFound);
  // Without a local file the found result is also handed to the requesting generation.
  job = {};
  job.op = FileOp::Remove;
  strcpy(job.path, lrcPath);
  assert(StorageTestAccess::run(resumed, job));
  assert(online.request(song, 93));
  online.process();
  assert(online.receive(parsed, 93, lyricsFound) && lyricsFound && parsed.synced &&
         parsed.count == 2 && parsed.line(1) == "Second line");
  // A user-provided LRC is never replaced by an online result.
  write(lrcPath, std::vector<uint8_t>{'u', 's', 'e', 'r'});
  assert(online.request(song, 94));
  online.process();
  assert(read(lrcPath).size() == 4 && httpRequests == 2);
  // Oversized, empty and malformed UTF-8 answers are rejected.
  SD.remove(lrcPath);
  size_t lyricBytes = 0;
  httpPacket.assign(maxLyricsBytes + 1, 'a');
  assert(!online.fetch(song, lyricBytes));
  httpPacket.clear();
  assert(!online.fetch(song, lyricBytes));
  httpPacket = {'[', '0', '0', ':', '0', '1', ']', 0xff};
  assert(!online.fetch(song, lyricBytes));
  Track nameless = song;
  nameless.artist[0] = 0;
  const auto before = httpRequests;
  assert(!online.fetch(nameless, lyricBytes) && httpRequests == before);
  assert(httpTimeout == 8000);

  // Invalid successful responses must never become a persistent cache entry.
  for (const std::string &invalid :
       {std::string("<html>gateway error</html>"), std::string("[00:01]ok\n[99:99]bad\n"),
        std::string("[00:01]") + std::string(1025, 'x'), std::string("[00:01]bad\0text", 16)}) {
    httpPacket.assign(invalid.begin(), invalid.end());
    assert(online.request(song, 95));
    online.process();
    assert(online.receive(parsed, 95, lyricsFound) && !lyricsFound);
    assert(!SD.exists(lrcPath));
  }
  httpDeclaredLength = -1;
  httpPacket.assign(maxLyricsBytes + 1, 'a');
  assert(!online.fetch(song, lyricBytes));
  httpDeclaredLength = -2;
  httpPacket.assign(lrc.begin(), lrc.end());
  httpComplete = false;
  assert(!online.fetch(song, lyricBytes));
  httpComplete = true;
  for (int status : {204, 404, 503}) {
    httpStatus = status;
    assert(online.request(song, 96));
    online.process();
    assert(online.receive(parsed, 96, lyricsFound) && !lyricsFound);
    assert(!SD.exists(lrcPath));
  }
  httpStatus = 200;
  httpReadChunk = 1;
  httpReadUs = 4000000;
  assert(!online.fetch(song, lyricBytes) && httpAt == 4);
  httpReadChunk = SIZE_MAX;
  httpReadUs = 0;

  // Save failure and an absent card still allow this track's validated RAM result.
  failWriteAfter = 0;
  assert(online.request(song, 97));
  online.process();
  failWriteAfter = -1;
  assert(online.receive(parsed, 97, lyricsFound) && lyricsFound && parsed.count == 2);
  assert(!SD.exists(lrcPath));
  StorageTestAccess::mounted(resumed, false);
  assert(online.request(song, 98));
  online.process();
  assert(online.receive(parsed, 98, lyricsFound) && lyricsFound);
  assert(!SD.exists(lrcPath));
  StorageTestAccess::mounted(resumed, true);

  // A local file arriving after HTTP starts wins at the final SD commit and RAM delivery.
  const std::vector<uint8_t> userLyrics{'u', 's', 'e', 'r'};
  httpBeforeRead = [&] { write(lrcPath, userLyrics); };
  assert(online.request(song, 99));
  online.process();
  assert(online.receive(parsed, 99, lyricsFound) && !lyricsFound);
  assert(read(lrcPath) == userLyrics);
  job = {};
  job.op = FileOp::LyricsSave;
  strcpy(job.path, lrcPath);
  job.data = reinterpret_cast<uint8_t *>(const_cast<char *>(lrc.data()));
  job.length = lrc.size();
  assert(!StorageTestAccess::run(resumed, job) && job.value == 1);
  strcpy(job.path, "/media/abcdefabcdef0123.lrc");
  assert(!StorageTestAccess::run(resumed, job));

  // A new track request made during a fetch survives delivery of the previous completion.
  SD.remove(lrcPath);
  Track nextSong = song;
  strcpy(nextSong.key, "012301230123abcd");
  httpBeforeRead = [&] { assert(online.request(nextSong, 101)); };
  assert(online.request(song, 100));
  online.process();
  const auto oldCount = parsed.count;
  assert(!online.receive(parsed, 101, lyricsFound) && parsed.count == oldCount);
  online.process();
  assert(online.receive(parsed, 101, lyricsFound) && lyricsFound);
  assert(SD.exists("/lyrics/012301230123abcd.lrc"));

  Track longSong = song;
  longSong.durationMs = UINT32_MAX;
  const auto priorRequests = httpRequests;
  assert(!online.fetch(longSong, lyricBytes) && httpRequests == priorRequests);
  std::cout
      << "Storage real worker handlers: checkpoint reboot, conflicting retry, atomic rollback, CRC "
         "rejection, JPEG/MSM seek, BMP orientation, Sync content identity, catalog, artwork "
         "pagination/protection/stale delivery, online lyrics cache/precedence, asset revision, "
         "cancellation, partial journal and write failure passed\n";
}

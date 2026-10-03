#include "../core/Text.h"
#include "../logging/Log.h"
#include "../media/Session.h"
#include "Journal.h"
#include "Storage.h"
#include <algorithm>
#include <cstring>
#include <new>
namespace nova {
namespace {
bool artworkPath(const char *path) {
  return strlen(path) == 29 && !strncmp(path, "/artwork/", 9) && !strcmp(path + 25, ".nvi");
}
bool automaticArtworkAllowed(const char *path) {
  char flag[144];
  for (const char *suffix : {"pin", "custom"}) {
    snprintf(flag, sizeof(flag), "%s.%s", path, suffix);
    if (SD.exists(flag))
      return false;
  }
  for (const char *suffix : {"block", "missing"}) {
    snprintf(flag, sizeof(flag), "%.25s.%s", path, suffix);
    if (SD.exists(flag))
      return false;
  }
  return true;
}
} // namespace
bool Storage::atomicWrite(const char *path, const uint8_t *data, size_t bytes) {
  if (!safeStoragePath(path) || !data)
    return false;
  char temp[144], backup[144];
  snprintf(temp, sizeof(temp), "%s.tmp", path);
  snprintf(backup, sizeof(backup), "%s.bak", path);
  SD.remove(temp);
  File f = SD.open(temp, FILE_WRITE);
  bool ok = bool(f);
  size_t at = 0;
  while (ok && at < bytes && !stopping_) {
    const size_t n = std::min(bytes - at, size_t(4096));
    ok = f.write(data + at, n) == n;
    at += n;
    vTaskDelay(1);
  }
  f.flush();
  f.close();
  ok = ok && at == bytes && !stopping_;
  f = SD.open(temp, FILE_READ);
  uint32_t crc = 0xFFFFFFFF;
  uint8_t check[1024];
  while (ok && f.available()) {
    const size_t n = f.read(check, sizeof(check));
    if (!n) {
      ok = false;
      break;
    }
    crc = crcUpdate(crc, check, n);
  }
  ok = ok && f.size() == bytes && ~crc == crc32(data, bytes);
  f.close();
  if (!ok) {
    SD.remove(temp);
    return false;
  }
  SD.remove(backup);
  const bool had = SD.exists(path);
  if (had && !SD.rename(path, backup))
    return false;
  if (!SD.rename(temp, path)) {
    if (had)
      SD.rename(backup, path);
    return false;
  }
  SD.remove(backup);
  return true;
}
void Storage::publishCatalog() {
  catalog_->version = ++catalogVersion_;
  mediaCount_ = catalog_->count;
  xSemaphoreTake(catalogMutex_, portMAX_DELAY);
  *published_ = *catalog_;
  xSemaphoreGive(catalogMutex_);
}
bool Storage::saveCatalog() {
  uint8_t *bytes = reinterpret_cast<uint8_t *>(buffer_->lyrics);
  const size_t n = 16 + catalog_->count * sizeof(MediaEntry);
  memset(bytes, 0, n);
  memcpy(bytes, "NMC1", 4);
  write32(bytes + 4, ++catalogSequence_);
  write32(bytes + 8, catalog_->count);
  memcpy(bytes + 16, catalog_->entries, catalog_->count * sizeof(MediaEntry));
  write32(bytes + 12, crc32(bytes + 16, n - 16));
  const char *path = (catalogSequence_ & 1) ? "/media/catalog.a" : "/media/catalog.b";
  const bool ok = atomicWrite(path, bytes, n);
  if (ok)
    publishCatalog();
  return ok;
}
bool Storage::scan() {
  decoder_.close();
  // Recover the newest validated catalog, then reconcile with files once per explicit scan.
  uint32_t best = catalogSequence_;
  for (const char *name : {"/media/catalog.a", "/media/catalog.b"}) {
    File f = SD.open(name, FILE_READ);
    if (!f || f.size() < 16 || f.size() > 16 + maxMediaEntries * sizeof(MediaEntry))
      continue;
    uint8_t *bytes = reinterpret_cast<uint8_t *>(buffer_->lyrics);
    const size_t n = f.size();
    if (f.read(bytes, n) != n || memcmp(bytes, "NMC1", 4))
      continue;
    const unsigned count = read32(bytes + 8);
    const uint32_t sequence = read32(bytes + 4);
    if (count > maxMediaEntries || n != 16 + count * sizeof(MediaEntry) ||
        crc32(bytes + 16, n - 16) != read32(bytes + 12))
      continue;
    auto *entries = reinterpret_cast<MediaEntry *>(bytes + 16);
    bool valid = true;
    for (unsigned i = 0; i < count; ++i)
      valid = valid && memchr(entries[i].path, 0, sizeof(entries[i].path)) &&
              memchr(entries[i].title, 0, sizeof(entries[i].title)) &&
              safeStoragePath(entries[i].path) && !strncmp(entries[i].path, "/media/", 7) &&
              mediaExtension(entries[i].path) && validUtf8(entries[i].title) &&
              reinterpret_cast<const uint8_t *>(&entries[i])[offsetof(MediaEntry, enabled)] <= 1 &&
              entries[i].seconds <= 3600;
    if (valid && (!best || int32_t(sequence - best) >= 0)) {
      catalog_->count = count;
      memcpy(catalog_->entries, entries, count * sizeof(MediaEntry));
      best = sequence;
    }
  }
  catalogSequence_ = best;
  // Reclaim interrupted replacement names, never format or delete unrelated files.
  for (const char *dir : {"/media", "/media/photo", "/media/video", "/artwork", "/update"}) {
    File folder = SD.open(dir);
    if (!folder)
      continue;
    for (File f = folder.openNextFile(); f; f = folder.openNextFile()) {
      char name[144];
      snprintf(name, sizeof(name), "%s", f.path());
      f.close();
      const size_t n = strlen(name);
      if (n > 4 && !strcmp(name + n - 4, ".bak")) {
        name[n - 4] = 0;
        if (!SD.exists(name)) {
          char backup[148];
          snprintf(backup, sizeof(backup), "%s.bak", name);
          SD.rename(backup, name);
        }
      }
    }
  }
  for (unsigned i = 0; i < catalog_->count;) {
    if (!SD.exists(catalog_->entries[i].path)) {
      for (unsigned n = i + 1; n < catalog_->count; ++n)
        catalog_->entries[n - 1] = catalog_->entries[n];
      --catalog_->count;
    } else
      ++i;
  }
  for (const char *dir : {"/media", "/media/photo", "/media/video"}) {
    File folder = SD.open(dir);
    if (!folder)
      continue;
    for (File f = folder.openNextFile(); f; f = folder.openNextFile()) {
      if (stopping_)
        return false;
      if (f.isDirectory())
        continue;
      const char *path = f.path();
      if (!safeStoragePath(path) || !mediaExtension(path) || !strncmp(path, "/media/sync.", 12))
        continue;
      bool exists = false;
      for (unsigned i = 0; i < catalog_->count; ++i)
        if (!strcmp(path, catalog_->entries[i].path)) {
          catalog_->entries[i].bytes = f.size();
          exists = true;
          break;
        }
      if (!exists && catalog_->count < maxMediaEntries) {
        auto &e = catalog_->entries[catalog_->count++];
        e = MediaEntry{};
        strcpy(e.path, path);
        cleanUtf8(e.title, sizeof(e.title), strrchr(path, '/') + 1);
        e.bytes = f.size();
      }
      vTaskDelay(1);
    }
  }
  return saveCatalog();
}
bool Storage::saveTransfer() {
  upload_.checksum = crc32(&upload_, offsetof(UploadRecord, checksum));
  return atomicWrite("/media/.transfer", reinterpret_cast<const uint8_t *>(&upload_),
                     sizeof(upload_));
}
void Storage::recoverTransfer() {
  File f = SD.open("/media/.transfer", FILE_READ);
  UploadRecord r{};
  if (f && f.size() == sizeof(r) &&
      f.read(reinterpret_cast<uint8_t *>(&r), sizeof(r)) == sizeof(r) && r.magic == 0x4E555031 &&
      r.id && memchr(r.path, 0, sizeof(r.path)) && safeStoragePath(r.path) &&
      r.total < 0x80000000U && r.offset <= r.total &&
      crc32(&r, offsetof(UploadRecord, checksum)) == r.checksum) {
    char part[144];
    snprintf(part, sizeof(part), "%s.part", r.path);
    File file = SD.open(part, FILE_READ);
    if (file && file.size() >= r.offset) {
      upload_ = r;
      log("SD", "recoverable upload offset=%lu", static_cast<unsigned long>(r.offset));
    }
  }
}
bool Storage::recoverLog() {
  const char *journal = "/logs/pending";
  if (!SD.exists(journal))
    return true;
  uint8_t record[logJournalMaximum];
  File f = SD.open(journal, FILE_READ);
  const size_t bytes = f ? f.size() : 0;
  if (!f || bytes > sizeof(record) || f.read(record, bytes) != bytes ||
      !validateLogJournal(record, bytes)) {
    f.close();
    return false;
  }
  f.close();
  const char *path = reinterpret_cast<const char *>(record + 4);
  const uint32_t offset = read32(record + 132), length = read32(record + 136);
  File out = SD.open(path, SD.exists(path) ? "r+" : FILE_WRITE);
  if (!out || out.size() < offset || out.size() > offset + length)
    return false;
  // Rewriting at the journal's fixed offset is idempotent after a partial or complete append.
  if (!out.seek(offset) || out.write(record + logJournalHeader, length) != length)
    return false;
  out.flush();
  out.close();
  out = SD.open(path, FILE_READ);
  uint8_t check[1024];
  const bool ok = out && out.size() == offset + length && out.seek(offset) &&
                  out.read(check, length) == length &&
                  !memcmp(check, record + logJournalHeader, length);
  out.close();
  return ok && SD.remove(journal);
}
void Storage::fileJob(FileJob &j) {
  j.ok = false;
  j.error[0] = 0;
  j.actual = 0;
  const auto fail = [&](const char *s) { cleanUtf8(j.error, sizeof(j.error), s); };
  if (j.path[0] && !safeStoragePath(j.path)) {
    fail("Invalid path");
    return;
  }
  if (j.op == FileOp::Info) {
    File f = SD.open(j.path);
    j.total = f ? f.size() : 0;
    j.capacity = SD.totalBytes();
    j.used = SD.usedBytes();
    j.ok = bool(f) || !j.path[0];
    return;
  }
  if (j.op == FileOp::Read) {
    File f = SD.open(j.path, FILE_READ);
    if (f && j.offset <= f.size() && f.seek(j.offset) && j.data) {
      j.total = f.size();
      j.actual = f.read(j.data, std::min<size_t>(j.length, f.size() - j.offset));
      j.ok = true;
    }
    return;
  }
  if (j.op == FileOp::List) {
    if (!j.data || j.length < sizeof(FileListing))
      return;
    auto &list = *new (j.data) FileListing{};
    File folder = SD.open(j.path);
    unsigned skipped = 0;
    for (File f = folder.openNextFile(); f && list.count < 32; f = folder.openNextFile()) {
      if (f.isDirectory())
        continue;
      const char *path = f.path();
      FileEntry item;
      if (!safeStoragePath(path) || strstr(path, ".tmp") || strstr(path, ".bak") ||
          strstr(path, ".part") || strstr(path, ".nix") || strstr(path, ".transfer") ||
          strstr(path, "/pending"))
        continue;
      strcpy(item.path, path);
      item.bytes = f.size();
      if (!strcmp(j.path, "/artwork")) {
        const size_t n = strlen(path);
        if (n == 33 && !strcmp(path + 25, ".missing")) {
          snprintf(item.path, sizeof(item.path), "%.25s.nvi", path);
          if (SD.exists(item.path))
            continue;
          item.missing = true;
          item.bytes = 0;
        } else if (n != 29 || strcmp(path + 25, ".nvi"))
          continue;
        char flag[144];
        snprintf(flag, sizeof(flag), "%s.pin", item.path);
        item.pinned = SD.exists(flag);
        snprintf(flag, sizeof(flag), "%s.custom", item.path);
        item.custom = SD.exists(flag);
        snprintf(flag, sizeof(flag), "%.25s.block", path);
        item.blocked = SD.exists(flag);
        snprintf(flag, sizeof(flag), "%.25s.meta", path);
        File meta = SD.open(flag);
        struct {
          Track track;
          uint32_t crc;
        } record{};
        if (meta && meta.size() == sizeof(record) &&
            meta.read(reinterpret_cast<uint8_t *>(&record), sizeof(record)) == sizeof(record) &&
            crc32(&record.track, sizeof(record.track)) == record.crc &&
            memchr(record.track.title, 0, sizeof(record.track.title)) &&
            memchr(record.track.artist, 0, sizeof(record.track.artist))) {
          char text[512];
          snprintf(text, sizeof(text), "%s · %s", record.track.title, record.track.artist);
          cleanUtf8(item.text, sizeof(item.text), text);
        }
        if (j.text[0] && !strstr(item.text, j.text) && !strstr(item.path, j.text))
          continue;
      }
      if (skipped++ < j.offset)
        continue;
      list.entries[list.count++] = item;
    }
    j.ok = true;
    j.actual = sizeof(list);
    return;
  }
  if (j.op == FileOp::ArtAllowed) {
    j.ok = artworkPath(j.path) && automaticArtworkAllowed(j.path);
    return;
  }
  if (j.op == FileOp::Write || j.op == FileOp::ArtSave || j.op == FileOp::LyricsSave) {
    if (j.op == FileOp::LyricsSave) {
      j.value = 0;
      char key[17]{};
      if (strlen(j.path) != 28 || strncmp(j.path, "/lyrics/", 8) || strcmp(j.path + 24, ".lrc")) {
        fail("Invalid lyrics cache path");
        return;
      }
      memcpy(key, j.path + 8, 16);
      if (!validTrackKey(key)) {
        fail("Invalid lyrics track key");
        return;
      }
      if (SD.exists(j.path)) {
        j.value = 1;
        fail("Local lyrics already exist");
        return;
      }
    }
    if (j.op == FileOp::ArtSave &&
        (!artworkPath(j.path) || (!j.enabled && !automaticArtworkAllowed(j.path)))) {
      fail("Automatic artwork is protected or blocked");
      return;
    }
    writing_ = true;
    decoder_.close();
    j.ok = atomicWrite(j.path, j.data, j.length);
    if (j.ok && (artworkPath(j.path) || !strncmp(j.path, "/lyrics/", 8)))
      ++assetRevision_;
    writing_ = false;
    return;
  }
  if (j.op == FileOp::Remove) {
    decoder_.close();
    j.ok = !SD.exists(j.path) || SD.remove(j.path);
    if (j.ok && (!strncmp(j.path, "/artwork/", 9) || !strncmp(j.path, "/lyrics/", 8)))
      ++assetRevision_;
    if (j.ok && !strncmp(j.path, "/artwork/", 9) && strlen(j.path) == 29 &&
        !strcmp(j.path + 25, ".nvi")) {
      char flag[144];
      for (const char *suffix : {"pin", "custom", "used"}) {
        snprintf(flag, sizeof(flag), "%s.%s", j.path, suffix);
        SD.remove(flag);
      }
      snprintf(flag, sizeof(flag), "%.25s.missing", j.path);
      const uint8_t marker = 1;
      j.ok = atomicWrite(flag, &marker, 1);
    }
    return;
  }
  if (j.op == FileOp::Validate || j.op == FileOp::SyncValidate) {
    uint32_t checksum = 0;
    const bool sync = j.op == FileOp::SyncValidate;
    if (sync) {
      File f = SD.open(j.path);
      uint8_t magic[4]{};
      if (!f || f.size() != j.offset || f.read(magic, 4) != 4 || memcmp(magic, "NJV1", 4)) {
        fail("Prepared Sync file does not match SD");
        return;
      }
    }
    j.ok = decoder_.validate(j.path, progress_, validationCancelled_, sync ? &checksum : nullptr);
    if (j.ok) {
      uint8_t header[64];
      File f = SD.open(j.path);
      MediaInfo info;
      if (f && mediaHeader(header, f.read(header, sizeof(header)), f.size(), info) &&
          (!sync || (!memcmp(header, "NJV1", 4) && f.size() == j.offset && checksum == j.checksum)))
        j.total = info.duration;
      else
        j.ok = false;
    }
    if (!j.ok)
      fail(sync ? "Prepared Sync file does not match SD or failed validation" : decoder_.error());
    return;
  }
  if (j.op == FileOp::Scan) {
    // Explicit repair also recovers a card removed after the initial mount.
    // Only this worker owns the open files and SPI host.
    decoder_.close();
    uploadFile_.close();
    SD.end();
    mounted_ = SD.begin(board::sdCs, spi_, board::sdHz);
    upload_ = UploadRecord{};
    catalog_->count = 0;
    catalogSequence_ = 0;
    if (mounted_) {
      for (const char *dir : {"/config", "/lyrics", "/artwork", "/media", "/logs", "/update"})
        SD.mkdir(dir);
      recoverTransfer();
    } else
      publishCatalog();
    j.ok = mounted_ && scan() && recoverLog();
    ++assetRevision_;
    return;
  }
  if (j.op == FileOp::MediaEdit) {
    if (j.id >= catalog_->count) {
      fail("Unknown media");
      return;
    }
    auto &e = catalog_->entries[j.id];
    if (j.text[0])
      cleanUtf8(e.title, sizeof(e.title), j.text);
    e.enabled = j.enabled;
    e.seconds = std::min<uint32_t>(j.value, 3600);
    if (j.offset < catalog_->count && j.offset != j.id) {
      const MediaEntry item = e;
      if (j.offset < j.id)
        for (unsigned i = j.id; i > j.offset; --i)
          catalog_->entries[i] = catalog_->entries[i - 1];
      else
        for (unsigned i = j.id; i < j.offset; ++i)
          catalog_->entries[i] = catalog_->entries[i + 1];
      catalog_->entries[j.offset] = item;
    }
    j.ok = saveCatalog();
    return;
  }
  if (j.op == FileOp::MediaDelete || j.op == FileOp::MediaClear) {
    decoder_.close();
    if (j.op == FileOp::MediaDelete && j.id >= catalog_->count) {
      fail("Unknown media");
      return;
    }
    bool ok = true;
    const unsigned start = j.op == FileOp::MediaClear ? 0 : j.id,
                   end = j.op == FileOp::MediaClear ? catalog_->count : j.id + 1;
    for (unsigned i = start; i < end; ++i) {
      const auto &path = catalog_->entries[i].path;
      ok = (!SD.exists(path) || SD.remove(path)) && ok;
      char idx[144];
      snprintf(idx, sizeof(idx), "%s.nix", path);
      SD.remove(idx);
    }
    j.ok = scan() && ok;
    return;
  }
  if (j.op == FileOp::UploadStatus) {
    j.id = upload_.id;
    j.total = upload_.total;
    j.offset = upload_.offset;
    strcpy(j.path, upload_.path);
    j.ok = true;
    return;
  }
  if (j.op == FileOp::UploadBegin) {
    if (!j.id || strlen(j.path) > 110 || j.total < 16 || j.total >= 0x80000000U ||
        (!mediaExtension(j.path) && strncmp(j.path, "/update/", 8))) {
      fail("Invalid transfer");
      return;
    }
    if (upload_.id == j.id && !strcmp(upload_.path, j.path) && upload_.total == j.total) {
      j.offset = upload_.offset;
      j.ok = true;
      return;
    }
    if (SD.exists(j.path) && !j.enabled) {
      fail("File already exists");
      return;
    }
    if (uint64_t(j.total) + 1048576 > SD.totalBytes() - SD.usedBytes()) {
      fail("Insufficient SD space");
      return;
    }
    uploadFile_.close();
    if (upload_.id) {
      char old[144];
      snprintf(old, sizeof(old), "%s.part", upload_.path);
      SD.remove(old);
    }
    upload_ = UploadRecord{};
    upload_.id = j.id;
    upload_.total = j.total;
    strcpy(upload_.path, j.path);
    char part[144];
    snprintf(part, sizeof(part), "%s.part", j.path);
    SD.remove(part);
    uploadFile_ = SD.open(part, FILE_WRITE);
    uploadFile_.close();
    j.ok = SD.exists(part) && saveTransfer();
    j.offset = 0;
    return;
  }
  if (j.op == FileOp::UploadAbort) {
    if (j.id && j.id != upload_.id) {
      fail("Wrong transfer");
      return;
    }
    uploadFile_.close();
    char part[144];
    snprintf(part, sizeof(part), "%s.part", upload_.path);
    SD.remove(part);
    SD.remove("/media/.transfer");
    upload_ = UploadRecord{};
    j.ok = true;
    return;
  }
  if (j.op == FileOp::UploadChunk) {
    if (!j.id || j.id != upload_.id || !j.data || !j.length || j.length > 262144 ||
        uint64_t(j.offset) + j.length > upload_.total || crc32(j.data, j.length) != j.checksum) {
      fail("Invalid chunk/CRC");
      return;
    }
    char part[144];
    snprintf(part, sizeof(part), "%s.part", upload_.path);
    if (j.offset < upload_.offset) {
      File f = SD.open(part, FILE_READ);
      uint8_t bytes[1024];
      uint32_t crc = 0xFFFFFFFF;
      size_t left = j.length;
      bool ok = f && uint64_t(j.offset) + j.length <= upload_.offset && f.seek(j.offset);
      while (ok && left) {
        size_t n = std::min(left, sizeof(bytes));
        ok = f.read(bytes, n) == n;
        crc = crcUpdate(crc, bytes, n);
        left -= n;
      }
      j.ok = ok && ~crc == j.checksum;
      j.offset = upload_.offset;
      if (!j.ok)
        fail("Conflicting retry");
      return;
    }
    if (j.offset != upload_.offset) {
      fail("Unexpected offset");
      j.offset = upload_.offset;
      return;
    }
    File f = SD.open(part, "r+");
    writing_ = true;
    bool ok = f && f.seek(upload_.offset);
    size_t at = 0;
    while (ok && at < j.length && !stopping_) {
      const size_t n = std::min(j.length - at, size_t(8192));
      ok = f.write(j.data + at, n) == n;
      at += n;
      vTaskDelay(1);
    }
    f.flush();
    f.close();
    if (ok && at == j.length) {
      const uint32_t durable = upload_.offset;
      upload_.offset += at;
      j.ok = saveTransfer();
      if (!j.ok)
        upload_.offset = durable;
    }
    writing_ = false;
    j.offset = upload_.offset;
    return;
  }
  if (j.op == FileOp::UploadCommit) {
    if (j.id != upload_.id || !j.id || upload_.offset != upload_.total) {
      fail("Incomplete transfer");
      return;
    }
    char part[144], backup[144];
    snprintf(part, sizeof(part), "%s.part", upload_.path);
    snprintf(backup, sizeof(backup), "%s.bak", upload_.path);
    writing_ = true;
    decoder_.close();
    bool ok = !strncmp(upload_.path, "/update/", 8) ||
              decoder_.validate(part, progress_, validationCancelled_);
    if (!ok) {
      fail(decoder_.error());
      writing_ = false;
      return;
    }
    SD.remove(backup);
    const bool old = SD.exists(upload_.path);
    if (old)
      ok = SD.rename(upload_.path, backup);
    if (ok)
      ok = SD.rename(part, upload_.path);
    if (!ok && old && !SD.exists(upload_.path))
      SD.rename(backup, upload_.path);
    if (ok) {
      char indexPart[160], indexFinal[144];
      snprintf(indexPart, sizeof(indexPart), "%s.nix", part);
      snprintf(indexFinal, sizeof(indexFinal), "%s.nix", upload_.path);
      if (SD.exists(indexPart)) {
        SD.remove(indexFinal);
        SD.rename(indexPart, indexFinal);
      }
      SD.remove(backup);
      strcpy(j.path, upload_.path);
      SD.remove("/media/.transfer");
      upload_ = UploadRecord{};
      if (!strncmp(j.path, "/media/", 7) && strncmp(j.path, "/media/sync.", 12))
        ok = scan();
    }
    writing_ = false;
    j.ok = ok;
    return;
  }
  if (j.op == FileOp::Log) {
    if (!j.data || !j.length || j.length > 1024 || strncmp(j.path, "/logs/", 6))
      return;
    if (!recoverLog()) {
      fail("Pending log recovery failed");
      return;
    }
    uint8_t pending[logJournalMaximum];
    File current = SD.open(j.path, FILE_READ);
    const uint32_t offset = current ? current.size() : 0;
    current.close();
    if (!encodeLogJournal(pending, sizeof(pending), j.path, offset, j.data, j.length))
      return;
    if (atomicWrite("/logs/pending", pending, logJournalHeader + j.length))
      j.ok = recoverLog();
    return;
  }
  if (j.op == FileOp::ArtRemember) {
    if (j.length != sizeof(Track) || !j.data)
      return;
    struct {
      Track track;
      uint32_t crc;
    } record{};
    memcpy(&record.track, j.data, sizeof(Track));
    if (!memchr(record.track.key, 0, 17) || !validTrackKey(record.track.key))
      return;
    record.crc = crc32(&record.track, sizeof(record.track));
    char path[128];
    snprintf(path, sizeof(path), "/artwork/%s.meta", record.track.key);
    j.ok = atomicWrite(path, reinterpret_cast<const uint8_t *>(&record), sizeof(record));
    return;
  }
  if (j.op == FileOp::ArtRefresh) {
    if (strncmp(j.path, "/artwork/", 9) || strlen(j.path) != 29)
      return;
    char flag[144];
    for (const char *suffix : {"pin", "custom"}) {
      snprintf(flag, sizeof(flag), "%s.%s", j.path, suffix);
      if (SD.exists(flag)) {
        fail("Pinned/custom artwork: unprotect before refresh");
        return;
      }
    }
    snprintf(flag, sizeof(flag), "%.25s.meta", j.path);
    struct {
      Track track;
      uint32_t crc;
    } record{};
    File meta = SD.open(flag);
    if (!meta || meta.size() != sizeof(record) ||
        meta.read(reinterpret_cast<uint8_t *>(&record), sizeof(record)) != sizeof(record) ||
        record.crc != crc32(&record.track, sizeof(record.track)) || !j.data ||
        j.length < sizeof(Track)) {
      fail("Track metadata unavailable");
      return;
    }
    if (!memchr(record.track.title, 0, 241) || !memchr(record.track.artist, 0, 241) ||
        !memchr(record.track.album, 0, 241) || !memchr(record.track.key, 0, 17) ||
        !validTrackKey(record.track.key))
      return;
    memcpy(j.data, &record.track, sizeof(Track));
    // Keep the last good image until a replacement has been fetched and committed.
    j.ok = true;
    for (const char *suffix : {"block", "missing"}) {
      snprintf(flag, sizeof(flag), "%.25s.%s", j.path, suffix);
      if (SD.exists(flag))
        j.ok = SD.remove(flag) && j.ok;
    }
    ++assetRevision_;
    return;
  }
  if (j.op == FileOp::ArtFlags) {
    if (strncmp(j.path, "/artwork/", 9) || strlen(j.path) != 29) {
      fail("Invalid artwork key");
      return;
    }
    char flag[144];
    const char *suffix = j.value == 1 ? "pin" : j.value == 2 ? "block" : "custom";
    if (j.value == 2)
      snprintf(flag, sizeof(flag), "%.25s.block", j.path);
    else
      snprintf(flag, sizeof(flag), "%s.%s", j.path, suffix);
    const uint8_t yes = 1;
    j.ok = j.enabled ? atomicWrite(flag, &yes, 1) : (!SD.exists(flag) || SD.remove(flag));
    if (j.ok)
      ++assetRevision_;
    return;
  }
  if (j.op == FileOp::ArtCleanup) {
    uint64_t total = 0;
    uint32_t oldest = UINT32_MAX;
    char candidate[128]{};
    File dir = SD.open("/artwork");
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
      if (f.isDirectory() || !strstr(f.name(), ".nvi") || strstr(f.name(), ".nvi."))
        continue;
      total += f.size();
      char flag[144];
      snprintf(flag, sizeof(flag), "%s.pin", f.path());
      bool locked = SD.exists(flag);
      snprintf(flag, sizeof(flag), "%s.custom", f.path());
      locked = locked || SD.exists(flag);
      snprintf(flag, sizeof(flag), "%s.used", f.path());
      File used = SD.open(flag);
      const uint32_t age = used ? used.getLastWrite() : f.getLastWrite();
      if (!locked && age <= oldest) {
        oldest = age;
        snprintf(candidate, sizeof(candidate), "%s", f.path());
      }
    }
    j.used = total;
    j.capacity = SD.totalBytes() - SD.usedBytes();
    j.ok = true;
    if (total > uint64_t(j.total) * 1048576 && candidate[0]) {
      decoder_.close();
      j.ok = SD.remove(candidate);
      char used[144];
      snprintf(used, sizeof(used), "%s.used", candidate);
      SD.remove(used);
      j.value = j.ok ? 1 : 0;
    }
    return;
  }
  fail("Unsupported storage operation");
}
} // namespace nova

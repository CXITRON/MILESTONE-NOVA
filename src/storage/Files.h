#pragma once
#include <cstddef>
#include <cstdint>
namespace nova {
constexpr unsigned maxMediaEntries = 64;
constexpr size_t legacyUploadChunk = 256 * 1024;
constexpr size_t maxUploadChunk = 2 * legacyUploadChunk;
struct MediaEntry {
  char path[121]{}, title[97]{};
  uint16_t seconds = 0; // Zero follows Settings::mediaSeconds; positive values override it.
  bool enabled = true;
  uint32_t duration = 0, bytes = 0;
};
struct MediaCatalog {
  uint32_t version = 0;
  unsigned count = 0;
  MediaEntry entries[maxMediaEntries]{};
};
struct FileEntry {
  char path[128]{}, text[193]{};
  uint32_t bytes = 0;
  bool pinned = false, custom = false, blocked = false, missing = false;
};
struct FileListing {
  unsigned count = 0;
  FileEntry entries[32]{};
};
enum class FileOp : uint8_t {
  Info,
  Read,
  Write,
  Remove,
  List,
  Scan,
  Validate,
  SyncValidate,
  MediaEdit,
  MediaDelete,
  MediaClear,
  UploadBegin,
  UploadChunk,
  UploadCommit,
  UploadAbort,
  UploadStatus,
  Log,
  ArtRemember,
  ArtRefresh,
  ArtFlags,
  ArtCleanup,
  ArtAllowed,
  ArtSave,
  LyricsSave
};
struct FileJob {
  FileOp op = FileOp::Info;
  char path[128]{}, text[193]{};
  uint8_t *data = nullptr;
  size_t length = 0, actual = 0;
  uint32_t offset = 0, total = 0, id = 0, checksum = 0, value = 0;
  uint32_t verifyUs = 0, writeUs = 0;
  uint64_t capacity = 0, used = 0;
  bool ok = false, enabled = true;
  char error[96]{};
};
struct UploadRecord {
  uint32_t magic = 0x4E555031, id = 0, total = 0, offset = 0;
  char path[128]{};
  uint32_t checksum = 0;
};
} // namespace nova

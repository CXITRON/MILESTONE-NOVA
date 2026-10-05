#pragma once
#include "../board/Board.h"
#include "../lyrics/Lyrics.h"
#include "../settings/Values.h"
#include "Files.h"
#include "MediaDecoder.h"
#include <SD.h>
#include <SPI.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
namespace nova {
enum class AssetKind : uint8_t { Track, Media, Stop };
struct AssetRequest {
  AssetKind kind = AssetKind::Track;
  uint32_t generation = 0, position = 0;
  char name[128]{};
};
struct AssetResult {
  AssetRequest request;
  bool lyricsPresent = false, artPresent = false, error = false, blocked = false;
  uint32_t durationMs = 0, frame = 0;
  size_t lyricsBytes = 0;
  char lyrics[maxLyricsBytes + 1]{};
  // Track results use artSide x artSide; media results use mediaSide x mediaSide.
  uint16_t pixels[board::mediaSide * board::mediaSide]{};
};
class Storage final : public LyricsProvider {
public:
  bool begin(Settings &, Secrets &, bool importSettings = true);
  bool request(const char *, uint32_t) override;
  bool requestMedia(unsigned item, uint32_t position, uint32_t generation);
  bool requestFile(const char *path, uint32_t position, uint32_t generation);
  AssetResult *receive();
  void release();
  void stop();
  bool stopped() const { return stopped_; }
  bool mounted() const { return mounted_; }
  unsigned mediaCount() const { return mediaCount_; }
  bool copyCatalog(MediaCatalog &out);
  uint32_t catalogVersion() const { return catalogVersion_; }
  uint32_t assetRevision() const { return assetRevision_; }
  // Blocking API for background I/O clients only. Caller owns all buffers until return.
  bool execute(FileJob &job);
  uint32_t progress() const { return progress_; }
  bool writing() const { return writing_; }
  void cancelValidation(bool cancel) { validationCancelled_ = cancel; }

private:
  friend struct StorageTestAccess;
  static void task(void *);
  void run();
  void importConfig(Settings &, Secrets &, bool);
  void readTrack(AssetResult &);
  void readMedia(AssetResult &);
  bool readExact(File &, uint8_t *, size_t);
  void fileJob(FileJob &);
  bool scan();
  bool saveCatalog();
  void publishCatalog();
  bool saveTransfer();
  void recoverTransfer();
  bool recoverLog();
  bool atomicWrite(const char *, const uint8_t *, size_t);
  SPIClass spi_{HSPI};
  QueueHandle_t requests_ = nullptr, results_ = nullptr, returned_ = nullptr, jobs_ = nullptr;
  SemaphoreHandle_t callMutex_ = nullptr, done_ = nullptr, catalogMutex_ = nullptr;
  AssetResult *buffer_ = nullptr;
  MediaCatalog *catalog_ = nullptr, *published_ = nullptr;
  MediaDecoder decoder_;
  UploadRecord upload_{};
  File uploadFile_;
  std::atomic<bool> stopping_{false}, stopped_{true}, mounted_{false}, writing_{false},
      validationCancelled_{false};
  std::atomic<unsigned> mediaCount_{0};
  static constexpr uint64_t artBytesUnknown = UINT64_MAX;
  std::atomic<uint64_t> artBytes_{artBytesUnknown};
  std::atomic<uint32_t> catalogVersion_{0}, assetRevision_{0}, progress_{0};
  uint32_t catalogSequence_ = 0;
  portMUX_TYPE jobGate_ = portMUX_INITIALIZER_UNLOCKED;
};
} // namespace nova

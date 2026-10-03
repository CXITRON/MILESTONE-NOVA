#pragma once
#include "../media/Session.h"
#include "../storage/Storage.h"
#include "Lyrics.h"
namespace nova {
// Online synced-lyrics lookup through the MILESTONE gateway. A found LRC is cached on SD as
// /lyrics/<key>.lrc; an existing local file (e.g. a portal upload) is never replaced.
class OnlineLyrics {
public:
  bool begin(Storage &storage);
  bool request(const Track &track, uint32_t generation);
  // Consumes a completed lookup. Returns true when one was available for `generation`;
  // `found` then reports whether `out` was filled.
  bool receive(Lyrics &out, uint32_t generation, bool &found);
  void process(); // Called only by the portal/service worker.
  bool fetch(const Track &query, size_t &bytes);

private:
  Storage *storage_ = nullptr;
  SemaphoreHandle_t mutex_ = nullptr;
  Track track_{};
  char *text_ = nullptr;
  Lyrics *result_ = nullptr;
  uint32_t requested_ = 0, delivered_ = 0;
  bool pending_ = false, ready_ = false, found_ = false;
};
} // namespace nova

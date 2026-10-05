#pragma once
#include <cstdint>
namespace nova {
// Main-loop state for local-first artwork/lyrics refreshes. Buffers stay owned by App.
struct TrackAssets {
  void invalidate(uint32_t now, bool trackChanged, bool retryOnline = false);
  void loaded(bool artwork, bool lyrics, bool blocked, bool error);
  bool canFetchArtwork(uint32_t now, bool storagePending) const;
  bool canFetchLyrics(bool storagePending) const;

  uint32_t generation = 1, artRetry = 0;
  bool loading = false, coverValid = false, artBlocked = false, artPending = false,
       lyricsLocal = false, lyricsRequested = false;
};
} // namespace nova

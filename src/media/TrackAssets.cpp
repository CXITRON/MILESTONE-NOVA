#include "TrackAssets.h"
namespace nova {
void TrackAssets::invalidate(uint32_t now, bool trackChanged, bool retryOnline) {
  ++generation;
  loading = true;
  artPending = false;
  if (trackChanged || retryOnline) {
    lyricsRequested = false;
    artRetry = now - 60000;
  }
  // A same-track SD revision is a refresh, not a new song. Keep its visible buffers
  // until the matching local result arrives, including when a download just saved them.
  if (trackChanged)
    coverValid = artBlocked = lyricsLocal = false;
}
void TrackAssets::loaded(bool artwork, bool lyrics, bool blocked, bool error) {
  loading = false;
  artBlocked = blocked;
  // A confirmed deletion clears the old asset; an I/O failure keeps the last good one.
  if (artwork || !error)
    coverValid = artwork;
  if (lyrics || !error)
    lyricsLocal = lyrics;
}
bool TrackAssets::canFetchArtwork(uint32_t now, bool storagePending) const {
  return !loading && !storagePending && !coverValid && !artBlocked && !artPending &&
         now - artRetry >= 60000;
}
bool TrackAssets::canFetchLyrics(bool storagePending) const {
  return !loading && !storagePending && !lyricsLocal && !lyricsRequested;
}
} // namespace nova

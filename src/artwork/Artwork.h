#pragma once
#include "../media/Session.h"
#include "../storage/Storage.h"
namespace nova {
class Artwork {
public:
  bool begin(Storage &storage);
  bool request(const Track &track, uint32_t generation, const Settings &settings);
  bool receive(uint16_t *pixels, uint32_t generation, bool &ok);
  void process(); // Called only by portal/service worker.
  void remember(const Track &track);
  bool fetch(const Track &query, uint16_t *pixels);
  bool save(const char *key, const uint16_t *pixels, bool custom, const Settings &settings);

private:
  Storage *storage_ = nullptr;
  SemaphoreHandle_t mutex_ = nullptr;
  Track track_{};
  Settings settings_{};
  uint8_t *packet_ = nullptr, *image_ = nullptr;
  uint16_t *result_ = nullptr;
  uint32_t requested_ = 0, delivered_ = 0;
  bool pending_ = false, ready_ = false, ok_ = false;
};
} // namespace nova

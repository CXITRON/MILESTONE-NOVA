#pragma once
#include <cstdint>
namespace nova {
// A freshly installed image stays PENDING_VERIFY until it has run long enough; resetting before
// that rolls back to the previous image. Confirmation is the only OTA duty left in the app.
class BootConfirm {
public:
  void begin();
  void tick(uint32_t now);
  bool pending() const { return pending_; }

private:
  bool pending_ = false;
  uint32_t started_ = 0, loops_ = 0;
};
} // namespace nova

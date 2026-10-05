#include "update/BootConfirm.h"
#include <esp_ota_ops.h>
#include <cassert>
#include <iostream>
namespace nova { void log(const char *, const char *, ...) {} }
using nova::BootConfirm;
int main() {
  // A normal boot has nothing to confirm.
  testBootState = 0;
  BootConfirm normal;
  normal.begin();
  assert(!normal.pending());
  normal.tick(1000000);
  assert(testConfirmCalls == 0);

  // A freshly installed image stays pending until 60 s and 500 loops have passed, even when the
  // millisecond clock wraps right after boot.
  testBootState = ESP_OTA_IMG_PENDING_VERIFY;
  testMillis = 0xfffffff0U;
  BootConfirm boot;
  boot.begin();
  assert(boot.pending());
  for (unsigned i = 0; i < 600; ++i)
    boot.tick(0x10U);
  assert(testConfirmCalls == 0 && boot.pending());

  // Low memory defers confirmation instead of cancelling the rollback guard.
  testHeap = 8000;
  boot.tick(70000);
  assert(testConfirmCalls == 0 && boot.pending());
  testHeap = 65536;

  // A failed confirmation retries one minute later; the second attempt succeeds.
  testConfirmResult = -1;
  boot.tick(70000);
  assert(testConfirmCalls == 1 && boot.pending());
  testConfirmResult = ESP_OK;
  boot.tick(129999);
  assert(testConfirmCalls == 1);
  boot.tick(130000);
  assert(testConfirmCalls == 2 && !boot.pending());

  // The image state query failing is treated as nothing pending.
  testStateResult = -1;
  testBootState = ESP_OTA_IMG_PENDING_VERIFY;
  BootConfirm unknown;
  unknown.begin();
  assert(!unknown.pending());
  std::cout << "Boot confirmation: loop/time/heap gates, clock wrap, retry and normal boot passed\n";
}

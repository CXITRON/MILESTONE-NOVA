#include "update/Recovery.h"
#include <cassert>
#include <cstring>
#include <iostream>
using nova::Recovery;
int main() {
  Recovery boot;
  boot.begin(0, false, false, true);
  assert(!boot.active() && boot.failures() == 0);
  for (unsigned n = 1; n <= 3; ++n) {
    boot.begin(0, false, true, false);
    assert(boot.failures() == n);
    assert(boot.active() == (n == 3));
  }
  for (unsigned i = 0; i < 600; ++i) boot.tick(100000);
  boot.begin(0, false, false, false);
  assert(boot.active()); // Recovery and an ordinary restart cannot silently clear the latch.
  boot.resumeNormal();
  boot.begin(0, false, false, false);
  assert(!boot.active());
  for (unsigned i = 0; i < 499; ++i) boot.tick(60000);
  boot.begin(0, false, true, false);
  assert(boot.failures() == 1);
  for (unsigned i = 0; i < 500; ++i) boot.tick(59999);
  boot.tick(60000);
  boot.begin(0, false, true, false);
  assert(boot.failures() == 0); // A late runtime crash is not an incomplete boot.
  for (unsigned i = 0; i < 500; ++i) boot.tick(60000);
  auto *lateOperation = new Recovery::FileScope("/media/late.njv");
  boot.begin(0, false, true, false);
  delete lateOperation;
  assert(!Recovery::blocked("/media/late.njv")); // A stable runtime crash need not be SD-related.
  boot.begin(0, true, false, true);
  assert(boot.active()); // Manual recovery works without any retained RAM.
  boot.begin(0xfffffff0U, false, false, true);
  for (unsigned i = 0; i < 600; ++i) boot.tick(0x10);
  boot.begin(0, false, true, false);
  assert(boot.failures() == 1); // Clock wrap did not count as a stable minute.
  boot.begin(0, false, false, true);
  // Emulate a crash while a nested operation scope is live, before its destructor.
  auto *folder = new Recovery::FileScope("/artwork");
  auto *file = new Recovery::FileScope("/artwork/abc.nvi");
  boot.begin(0, false, true, false);
  assert(Recovery::blocked("/artwork/abc.nvi"));
  assert(!Recovery::blocked("/artwork/abc.nvi2"));
  assert(!Recovery::blocked("/media/ok.nvi"));
  char path[128]; Recovery::suspect(path, sizeof(path));
  assert(!strcmp(path, "/artwork/abc.nvi"));
  delete file; delete folder;
  boot.resumeNormal();
  assert(!Recovery::blocked("/artwork/abc.nvi"));
  boot.begin(0, false, false, false); // resumeNormal requests a fresh normal boot.
  auto *directory = new Recovery::FileScope("/artwork");
  boot.begin(0, false, true, false);
  assert(Recovery::blocked("/artwork/abc.nvi"));
  assert(!Recovery::blocked("/artwork2/abc.nvi"));
  delete directory;
  boot.begin(0, false, false, true);
  assert(!Recovery::blocked("/artwork/abc.nvi"));
  std::cout << "Recovery: manual, repeated crash, stable runtime, wrap, retained latch and non-destructive path quarantine passed\n";
}

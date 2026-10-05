#include "update/AutoUpdate.h"
#include "update/Release.h"
#include <cassert>
#include <iostream>
using namespace nova;
int main() {
  int order = 7;
  assert(compareReleaseVersions("0.10.0", "0.2.0", order) && order > 0);
  assert(compareReleaseVersions("1.0.0", "1.0.0", order) && order == 0);
  assert(compareReleaseVersions("1.2.8", "1.2.9", order) && order < 0);
  for (const char *bad : {"", "1", "1.2", "01.2.3", "1.02.3", "1.2.3.4", "1.2.3-rc1",
                         "v1.2.3", "1.2.3+build", "1.2.3 ", "1.2.-1", "1000000.0.0"})
    assert(!compareReleaseVersions(bad, "0.1.0", order));
  const char *good = "https://github.com/CXITRON/MILESTONE-NOVA/releases/download/v1.2.3/nova.bin";
  assert(releaseAssetUrl(good, "1.2.3") && releaseRedirect(good));
  assert(!releaseAssetUrl(good, "1.2.4"));
  for (const char *bad : {"https://github.com/other/repo/releases/download/v1.2.3/nova.bin",
                         "https://github.com/CXITRON/MILESTONE-NOVA/releases/download/v1.2.3/../nova.bin",
                         "https://github.com/CXITRON/MILESTONE-NOVA/releases/download/v1.2.3/%2e%2e.bin",
                         "https://github.com/CXITRON/MILESTONE-NOVA/releases/download/v1.2.3/nova.bin?x=1",
                         "https://github.com/CXITRON/MILESTONE-NOVA/releases/latest/download/nova.bin",
                         "https://raw.githubusercontent.com/CXITRON/MILESTONE-NOVA/main/nova.bin"})
    assert(!releaseAssetUrl(bad, "1.2.3"));
  using A = AutoUpdate::Action;
  using R = ReleaseState;
  constexpr uint32_t base = 0xfffffff0U, settle = AutoUpdate::settleMs, day = AutoUpdate::dailyMs,
                     hour = AutoUpdate::retryMs;
  const auto t = [](uint32_t ms) { return static_cast<uint32_t>(base + ms); };
  {
    // Boot check once Wi-Fi allows it, then daily; a found release is only announced.
    AutoUpdate scheduler;
    scheduler.begin(base);
    assert(scheduler.next(t(100), false, true, false, R::Idle) == A::None);
    assert(scheduler.next(t(settle - 1), false, true, false, R::Idle) == A::None);
    assert(scheduler.next(t(settle), false, false, false, R::Idle) == A::None);
    assert(scheduler.next(t(settle + 5000), false, true, true, R::Idle) == A::None);
    assert(scheduler.next(t(settle + 5000), false, true, false, R::Idle) == A::Check);
    assert(scheduler.next(t(settle + 5001), false, true, true, R::Working) == A::None);
    assert(scheduler.next(t(settle + 5002), false, true, false, R::Available) == A::None);
    assert(scheduler.next(t(settle + 5000 + day - 1), false, true, false, R::Available) == A::None);
    assert(scheduler.next(t(settle + 5000 + day), false, true, false, R::Available) == A::Check);
    assert(scheduler.next(t(settle + 5001 + day), false, true, false, R::Current) == A::None);
    assert(scheduler.next(t(settle + 5000 + 2 * day - 1), false, true, false, R::Current) == A::None);
    assert(scheduler.next(t(settle + 5000 + 2 * day), false, true, false, R::Current) == A::Check);
  }
  {
    // A failed check (offline, no release yet) is retried hourly, not every poll and not daily.
    AutoUpdate scheduler;
    scheduler.begin(base);
    assert(scheduler.next(t(settle), true, true, false, R::Idle) == A::Check);
    assert(scheduler.next(t(settle + 1), true, true, false, R::Failed) == A::None);
    assert(scheduler.next(t(settle + hour - 1), true, true, false, R::Failed) == A::None);
    assert(scheduler.next(t(settle + hour), true, true, false, R::Failed) == A::Check);
  }
  {
    // Automatic install continues check -> download -> install and stops on any failure.
    AutoUpdate scheduler;
    scheduler.begin(base);
    assert(scheduler.next(t(settle), true, true, false, R::Idle) == A::Check);
    assert(scheduler.next(t(settle + 1), true, true, true, R::Available) == A::None);
    assert(scheduler.next(t(settle + 2), true, true, false, R::Available) == A::Download);
    assert(scheduler.next(t(settle + 3), true, false, false, R::Ready) == A::None);
    assert(scheduler.next(t(settle + 4), true, true, false, R::Ready) == A::Install);
    AutoUpdate failing;
    failing.begin(base);
    assert(failing.next(t(settle), true, true, false, R::Idle) == A::Check);
    assert(failing.next(t(settle + 1), true, true, false, R::Available) == A::Download);
    assert(failing.next(t(settle + 2), true, true, false, R::Failed) == A::None);
    // Turning automatic install off mid-chain downloads nothing further.
    AutoUpdate cancelled;
    cancelled.begin(base);
    assert(cancelled.next(t(settle), true, true, false, R::Idle) == A::Check);
    assert(cancelled.next(t(settle + 1), false, true, false, R::Available) == A::None);
  }
  {
    // A verified candidate waiting for the user is never replaced by a routine daily check.
    AutoUpdate scheduler;
    scheduler.begin(base);
    assert(scheduler.next(t(settle + day * 3), false, true, false, R::Ready) == A::None);
    assert(scheduler.next(t(settle + day * 3), false, true, false, R::Success) == A::None);
    assert(scheduler.next(t(settle + day * 3), false, true, false, R::Available) == A::Check);
  }
  std::cout << "Release versions, tag/asset identity and boot/daily/hourly-retry scheduling and automatic-install chain passed\n";
}

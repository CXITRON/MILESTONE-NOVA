#include "AutoUpdate.h"
namespace nova {
AutoUpdate::Action AutoUpdate::next(uint32_t now, bool autoInstall, bool permitted, bool busy,
                                   ReleaseState state) {
  if (!permitted || busy)
    return Action::None;
  if (pending_ != Action::None) {
    const Action finished = pending_;
    pending_ = Action::None;
    if (finished == Action::Check) {
      interval_ = state == ReleaseState::Failed ? retryMs : dailyMs;
      if (autoInstall && state == ReleaseState::Available)
        return pending_ = Action::Download;
    } else if (finished == Action::Download && autoInstall && state == ReleaseState::Ready)
      return pending_ = Action::Install;
    return Action::None;
  }
  // A verified candidate waiting for installation must not be dropped by a routine check.
  if (state == ReleaseState::Ready || state == ReleaseState::Success)
    return Action::None;
  if (now - last_ < interval_)
    return Action::None;
  last_ = now;
  return pending_ = Action::Check;
}
} // namespace nova

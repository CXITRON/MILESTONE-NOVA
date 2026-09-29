#include "Navigation.h"
namespace nova {
Screen Navigation::coreScreen(unsigned i) {
  constexpr Screen pages[]{Screen::Clock,     Screen::DDay,        Screen::Message,
                           Screen::Dashboard, Screen::DateMessage, Screen::DDayClock,
                           Screen::System,    Screen::Timer,       Screen::Environment};
  return pages[i % 9];
}
void Navigation::select(Profile p) {
  profile_ = p;
  screen_ = p == Profile::Core    ? coreScreen(core_)
            : p == Profile::Media ? Screen::Media
                                  : Screen::Now;
  previous_ = screen_;
}
void Navigation::openMenu() {
  if (screen_ == Screen::Menu) {
    back();
    return;
  }
  previous_ = screen_;
  screen_ = Screen::Menu;
  menu_ = unsigned(profile_);
}
void Navigation::back() {
  if (screen_ == Screen::Menu)
    screen_ = previous_;
  else
    select(profile_);
}
void Navigation::move(int d, uint16_t mask, const uint8_t *order) {
  d = d < 0 ? -1 : 1;
  if (screen_ == Screen::Menu) {
    menu_ = (int(menu_) + int(MenuAction::Count) + d) % int(MenuAction::Count);
    return;
  }
  if (profile_ != Profile::Core || screen_ != coreScreen(core_) || !(mask & 511))
    return;
  unsigned at = core_;
  if (order)
    for (unsigned i = 0; i < 9; ++i)
      if (order[i] == core_)
        at = i;
  for (unsigned i = 0; i < 9; ++i) {
    at = (int(at) + 9 + d) % 9;
    const unsigned next = order ? order[at] : at;
    if (next < 9 && (mask & (1U << next))) {
      core_ = next;
      screen_ = coreScreen(core_);
      break;
    }
  }
}
} // namespace nova

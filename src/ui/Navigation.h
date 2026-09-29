#pragma once
#include <cstdint>
namespace nova {
enum class Profile : uint8_t { Core, Media, Now };
enum class Screen : uint8_t {
  Clock,
  DDay,
  Timer,
  Environment,
  System,
  Now,
  Media,
  Settings,
  Boot,
  Menu,
  Setup,
  Updates,
  Recovery,
  Message,
  Dashboard,
  DateMessage,
  DDayClock,
  Diagnostics
};
enum class MenuAction : uint8_t {
  Core,
  Media,
  Now,
  Settings,
  Setup,
  Updates,
  Recovery,
  Diagnostics,
  Restart,
  Off,
  Count
};
class Navigation {
public:
  void select(Profile profile);
  void core(unsigned index) {
    core_ = index % 9;
    if (profile_ == Profile::Core)
      select(profile_);
  }
  void openMenu();
  void back();
  void move(int direction, uint16_t coreMask = 511, const uint8_t *order = nullptr);
  MenuAction action() const { return static_cast<MenuAction>(menu_); }
  void page(Screen screen) { screen_ = screen; }
  Profile profile() const { return profile_; }
  Screen screen() const { return screen_; }
  unsigned menu() const { return menu_; }
  unsigned core() const { return core_; }
  static Screen coreScreen(unsigned index);

private:
  Profile profile_ = Profile::Core;
  Screen screen_ = Screen::Clock, previous_ = Screen::Clock;
  unsigned menu_ = 0, core_ = 0;
};
} // namespace nova

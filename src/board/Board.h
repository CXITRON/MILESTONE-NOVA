#pragma once
#include <array>
#include <cstdint>

namespace nova::board {
inline constexpr char version[] = "0.1.0";
inline constexpr int red = 1, rgb = 2, battery = 3, ok = 5;
inline constexpr int lcdMosi = 6, lcdSck = 7, green = 8, sda = 9, scl = 10;
inline constexpr int sdMosi = 11, sdSck = 12, sdMiso = 13;
inline constexpr int lcdCs = 15, lcdReset = 16, lcdDc = 17, backlight = 18;
inline constexpr int onboardRgb = 38, back = 39, prev = 40, next = 41, menu = 42;
inline constexpr int uartTx = 43, uartRx = 44, sdCs = 46;
inline constexpr int width = 240, height = 320, artSize = 160, rgbCount = 5;
inline constexpr uint32_t lcdHz = 20000000, sdHz = 10000000, i2cHz = 100000;
inline constexpr std::array<int, 5> buttons{back, prev, ok, next, menu};
inline constexpr std::array<int, 23> used{red,   rgb,      battery, ok,        lcdMosi,    lcdSck,
                                          green, sda,      scl,     sdMosi,    sdSck,      sdMiso,
                                          lcdCs, lcdReset, lcdDc,   backlight, onboardRgb, back,
                                          prev,  next,     menu,    uartTx,    uartRx};
constexpr bool allowed(int pin) {
  return pin != 0 && pin != 4 && pin != 14 && pin != 19 && pin != 20 && pin != 21 && pin != 45 &&
         pin != 47 && pin != 48 && !(pin >= 26 && pin <= 37);
}
constexpr bool pinsValid() {
  for (unsigned i = 0; i < used.size(); ++i) {
    if (!allowed(used[i]) || used[i] == sdCs)
      return false;
    for (unsigned j = i + 1; j < used.size(); ++j)
      if (used[i] == used[j])
        return false;
  }
  return allowed(sdCs) && ok >= 0 && ok <= 21;
}
static_assert(pinsValid(), "GPIO conflict or reserved GPIO in board map");
} // namespace nova::board

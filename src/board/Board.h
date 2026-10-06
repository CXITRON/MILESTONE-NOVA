#pragma once
#include <array>
#include <cstdint>

namespace nova::board {
// Release builds pass the bare number (-DNOVA_VERSION_NUMBER=0.1.1, one preprocessing token, so no
// shell/arduino-cli quoting is involved); tests may define the string NOVA_VERSION directly.
#ifndef NOVA_VERSION
#ifdef NOVA_VERSION_NUMBER
#define NOVA_VERSION_TEXT_(x) #x
#define NOVA_VERSION_TEXT(x) NOVA_VERSION_TEXT_(x)
#define NOVA_VERSION NOVA_VERSION_TEXT(NOVA_VERSION_NUMBER)
#else
#define NOVA_VERSION "0.1.0"
#endif
#endif
inline constexpr char version[] = NOVA_VERSION;
static_assert(sizeof(version) <= 32, "Version must fit the signed application descriptor");
inline constexpr int rgb = 2, battery = 3, ok = 5;
inline constexpr int lcdMosi = 6, lcdSck = 7, sda = 9, scl = 10;
inline constexpr int sdMosi = 11, sdSck = 12, sdMiso = 13;
inline constexpr int lcdCs = 15, lcdReset = 16, lcdDc = 17, backlight = 18;
inline constexpr int onboardRgb = 38, back = 39, prev = 40, next = 41, menu = 42;
inline constexpr int uartTx = 43, uartRx = 44, sdCs = 46;
inline constexpr int width = 240, height = 320, rgbCount = 5;
// Square pixel sides: NOW artwork and full-width local MEDIA.
inline constexpr unsigned artSide = 200, mediaSide = 240;
// The SD slot is on the board, so its traces are short: try 20 MHz first and fall back to the
// 10 MHz that was used until now if the card refuses to mount at the faster clock.
inline constexpr uint32_t lcdHz = 20000000, sdHz = 20000000, sdSafeHz = 10000000, i2cHz = 100000;
// ST7789V MADCTL for the mounted panel: MY|MX (0xC0) rotates 180 degrees so the image is upright.
inline constexpr uint8_t lcdMadctl = 0xC0;
// SK6812 data enters the rightmost LED, so chain index 0 is the right end.
inline constexpr bool rgbFromRight = true;
inline constexpr std::array<int, 5> buttons{back, prev, ok, next, menu};
inline constexpr std::array<int, 21> used{rgb,    battery,  ok,    lcdMosi,   lcdSck,     sda,
                                          scl,    sdMosi,   sdSck, sdMiso,    lcdCs,      lcdReset,
                                          lcdDc,  backlight, back, prev,      next,       menu,
                                          onboardRgb, uartTx, uartRx};
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

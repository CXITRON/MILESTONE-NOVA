#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
font=${U8G2_CLIB:-$HOME/Arduino/libraries/U8g2/src/clib}
[[ -f "$font/u8g2_font.c" ]] || { echo 'Set U8G2_CLIB to the installed U8g2 src/clib directory.' >&2; exit 2; }
out="$root/build/preview"
mkdir -p "$out"
for source in u8g2_font u8g2_hvline u8g2_intersection u8g2_fonts u8g2_setup u8g2_ll_hvline u8x8_8x8; do
  "${CC:-gcc}" -O1 -g -ffunction-sections -fdata-sections -I"$font" \
    -c "$font/$source.c" -o "$out/$source.o"
done
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -g -O1 \
  -fsanitize=address,undefined -fno-omit-frame-pointer -ffunction-sections -fdata-sections \
  -I"$root/src" -I"$font" "$root/tests/render/render_ui.cpp" \
  "$root/src/display/Canvas.cpp" "$root/src/ui/Ui.cpp" "$root/src/ui/CoreScreens.cpp" \
  "$root/src/ui/NowScreen.cpp" "$root/src/ui/SettingsScreen.cpp" \
  "$root/src/lyrics/Renderer.cpp" "$root/src/lyrics/Lyrics.cpp" \
  "$root/src/core/Text.cpp" "$root/src/core/Logic.cpp" "$root/src/media/Session.cpp" \
  "$out"/*.o -Wl,--gc-sections -o "$out/render"
ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0} "$out/render" "$out"
python3 "$root/tests/render/contact_sheet.py" "$out"

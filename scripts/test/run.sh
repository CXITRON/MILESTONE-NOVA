#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
mkdir -p "$root/build/tests"
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -pedantic -g -O1 \
  -fsanitize=address,undefined -fno-omit-frame-pointer -I"$root/src" \
  "$root/tests/test_core.cpp" \
  "$root/src/core/Text.cpp" "$root/src/core/Logic.cpp" \
  "$root/src/input/Button.cpp" "$root/src/input/InputEvents.cpp" "$root/src/settings/Values.cpp" \
  "$root/src/lyrics/Lyrics.cpp" "$root/src/media/Session.cpp" \
  "$root/src/media/HelperProtocol.cpp" "$root/src/media/AmsDecoder.cpp" -o "$root/build/tests/core"
ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0} "$root/build/tests/core"
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -pedantic -g -O1 \
  -fsanitize=address,undefined -fno-omit-frame-pointer -I"$root/src" \
  "$root/tests/test_parity.cpp" "$root/src/core/Text.cpp" "$root/src/core/Logic.cpp" \
  "$root/src/settings/Values.cpp" "$root/src/media/Formats.cpp" "$root/src/media/Playback.cpp" \
  "$root/src/media/Session.cpp" "$root/src/media/AmsDecoder.cpp" "$root/src/media/TrackAssets.cpp" \
  "$root/src/storage/Journal.cpp" "$root/src/ui/Navigation.cpp" -o "$root/build/tests/parity"
ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0} "$root/build/tests/parity"
node "$root/tests/test_portal.mjs"
python3 -m unittest discover -s "$root/tests" -p 'test_*.py'
"$root/scripts/test/storage.sh"

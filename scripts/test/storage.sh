#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
mkdir -p "$root/build/tests"
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -g -O1 \
  -fsanitize=address,undefined -fno-omit-frame-pointer -I"$root/tests/storage/support" -I"$root/src" \
  "$root/tests/storage/test_storage.cpp" "$root/tests/storage/support/Jpeg.cpp" \
  "$root/src/storage/StorageFiles.cpp" "$root/src/storage/MediaDecoder.cpp" "$root/src/storage/Journal.cpp" \
  "$root/src/artwork/Artwork.cpp" \
  "$root/src/core/Text.cpp" "$root/src/media/Formats.cpp" "$root/src/media/JpegImage.cpp" -ljpeg -o "$root/build/tests/storage"
fixture=$(mktemp -d /tmp/nova-storage-test.XXXXXX)
trap 'rm -rf -- "$fixture"' EXIT
ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0} "$root/build/tests/storage" "$fixture"

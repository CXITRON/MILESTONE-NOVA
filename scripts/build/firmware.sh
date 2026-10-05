#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cli=${ARDUINO_CLI:-}
if [[ -z $cli ]]; then
  cli=$(command -v arduino-cli || true)
fi
if [[ -z $cli && -x /opt/arduino-ide/resources/app/lib/backend/resources/arduino-cli ]]; then
  cli=/opt/arduino-ide/resources/app/lib/backend/resources/arduino-cli
fi
[[ -x $cli ]] || { echo 'Set ARDUINO_CLI to arduino-cli.' >&2; exit 2; }
stage="$root/build/sketch/Nova"
mkdir -p "$stage" "$root/build/firmware"
rm -rf -- "$stage/src"
cp -R "$root/src" "$stage/src"
python3 "$root/scripts/build/embed_portal.py" "$root/assets/portal" "$stage/src/portal/Assets.cpp"
cp "$root/partitions.csv" "$stage/partitions.csv"
printf '// Generated staging sketch. Sources are in src/.\n' > "$stage/Nova.ino"
fqbn='esp32:esp32:lolin_s3:FlashMode=qio,USBMode=hwcdc,CDCOnBoot=cdc,PartitionScheme=app3M_fat9M_16MB'
extra_flags='-DUPDATE_SIGN -std=gnu++17'
if [[ -n ${NOVA_VERSION:-} ]]; then
  [[ $NOVA_VERSION =~ ^(0|[1-9][0-9]{0,5})\.(0|[1-9][0-9]{0,5})\.(0|[1-9][0-9]{0,5})$ ]] || {
    echo 'NOVA_VERSION must be a stable major.minor.patch version.' >&2
    exit 2
  }
  extra_flags+=" -DNOVA_VERSION_NUMBER=$NOVA_VERSION"
fi
"$cli" compile --fqbn "$fqbn" --build-path "$root/build/firmware" \
  --build-property 'upload.maximum_size=6291456' \
  --build-property "compiler.cpp.extra_flags=$extra_flags" \
  --build-property 'compiler.c.elf.extra_flags=-Wl,--wrap=esp_mbedtls_mem_calloc' \
  --warnings all "$stage" "$@"

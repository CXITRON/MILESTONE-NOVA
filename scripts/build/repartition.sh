#!/usr/bin/env bash
# One-time USB step: switch a device from the 20 KiB NVS layout to the 64 KiB one (partitions.csv).
# The saved settings and Wi-Fi profiles are kept: NVS starts at the same flash offset, and only
# the area after the old NVS (old otadata and the head of the old app) is erased.
# Usage: NOVA_VERSION=x.y.z scripts/build/repartition.sh /dev/ttyACM0   (or --check to only build)
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
port=${1:-}
[[ -n $port ]] || { echo "usage: NOVA_VERSION=x.y.z $0 PORT|--check" >&2; exit 2; }
: "${NOVA_VERSION:?set NOVA_VERSION to the released version, for example 0.1.10}"
esptool=${ESPTOOL:-$(ls "$HOME"/.arduino15/packages/esp32/tools/esptool_py/*/esptool 2>/dev/null | sort -V | tail -1 || true)}
[[ $port == --check || -x ${esptool:-} ]] || { echo 'Set ESPTOOL to the esptool binary.' >&2; exit 2; }
out="$root/build/repartition"
mkdir -p "$out"
echo "Building NOVA $NOVA_VERSION with the new partition table..."
"$root/scripts/build/firmware.sh" >"$out/build.log" 2>&1 || { tail -20 "$out/build.log" >&2; exit 1; }
build="$root/build/firmware"
python3 - "$build/Nova.ino.partitions.bin" <<'PY'
import struct, sys
data = open(sys.argv[1], "rb").read()
found = {}
for at in range(0, len(data) - 32, 32):
    if data[at:at + 2] != b"\xaa\x50":
        continue
    _, ptype, subtype, offset, size = struct.unpack("<HBBII", data[at:at + 12])
    found[data[at + 12:at + 28].split(b"\0")[0].decode()] = (offset, size)
expected = {"nvs": (0x9000, 0x10000), "otadata": (0x19000, 0x2000),
            "app0": (0x20000, 0x600000), "app1": (0x620000, 0x600000)}
if found != expected:
    sys.exit(f"Unexpected partition table: {found}")
print("Partition table OK:", ", ".join(f"{k} 0x{v[0]:x}+0x{v[1]:x}" for k, v in found.items()))
PY
[[ $port == --check ]] && exit 0
stamp=$(date +%Y%m%d-%H%M%S)
echo "Backing up the current NVS to $out/nvs-backup-$stamp.bin ..."
"$esptool" --port "$port" read-flash 0x9000 0x5000 "$out/nvs-backup-$stamp.bin"
read -r -p "Rewrite the partition table and app now? Settings are kept. [y/N] " answer
[[ $answer == y ]] || { echo 'Cancelled; nothing was changed.'; exit 1; }
# Old otadata and the head of the old app now lie inside the larger NVS; erase them first.
"$esptool" --port "$port" erase-region 0xE000 0x12000
"$esptool" --port "$port" write-flash \
  0x20000 "$build/Nova.ino.bin" 0x19000 "$build/boot_app0.bin" 0x8000 "$build/Nova.ino.partitions.bin"
echo 'Done. The device restarts into the new layout.'

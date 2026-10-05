#!/usr/bin/env bash
# Prepare offline once, then apply the verified bundle with USB (no rebuild needed).
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
case "${1:-}" in
  --check)
    : "${NOVA_VERSION:?set NOVA_VERSION, for example 0.1.11}"
    mkdir -p "$root/build/repartition"
    "$root/scripts/build/firmware.sh" >"$root/build/repartition/build.log" 2>&1 || {
      tail -30 "$root/build/repartition/build.log" >&2; exit 1;
    }
    python3 "$root/scripts/build/repartition.py" prepare --version "$NOVA_VERSION"
    ;;
  --inspect)
    python3 "$root/scripts/build/repartition.py" inspect
    ;;
  /dev/*)
    python3 "$root/scripts/build/repartition.py" apply --port "$1"
    ;;
  *) echo "Usage: NOVA_VERSION=x.y.z $0 --check | $0 --inspect | $0 /dev/ttyACM0" >&2; exit 2;;
esac

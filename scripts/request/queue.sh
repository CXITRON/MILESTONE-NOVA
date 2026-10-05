#!/usr/bin/env bash
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
ACTIVE_DIR="$ROOT/docs/agents/requests/active"

if [[ ! -d "$ACTIVE_DIR" ]]; then
    echo "No active request directory."
    exit 0
fi

mapfile -t files < <(
    find "$ACTIVE_DIR" -maxdepth 1 -type f \
        -name 'request_U[0-9][0-9][0-9][0-9]_[EHNL]_*.md' \
        -printf '%f\n' |
    awk '
        {
            if ($0 ~ /_E_/) p=1;
            else if ($0 ~ /_H_/) p=2;
            else if ($0 ~ /_N_/) p=3;
            else if ($0 ~ /_L_/) p=4;
            else p=9;

            id=$0;
            sub(/^request_U/, "", id);
            sub(/_.*/, "", id);

            printf "%d %04d %s\n", p, id+0, $0;
        }
    ' |
    sort -k1,1n -k2,2n |
    cut -d' ' -f3-
)

if (( ${#files[@]} == 0 )); then
    echo "No active requests."
    exit 0
fi

printf 'Priority order: E > H > N > L\n\n'

for file in "${files[@]}"; do
    printf '%s\n' "$file"
done

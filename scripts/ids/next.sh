#!/usr/bin/env bash
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
REQUEST_DIR="$ROOT/docs/agents/requests"
REPORT_DIR="$ROOT/docs/agents/reports"

last_u="$(
  find "$REQUEST_DIR" -type f \
    \( -name 'request_U[0-9][0-9][0-9][0-9]_[EHNL]_*.md' \
       -o -name 'request_U[0-9][0-9][0-9][0-9]_[0-9][0-9][0-9][0-9]_[0-9][0-9]_[0-9][0-9].md' \) \
    -printf '%f\n' 2>/dev/null |
  sed -n 's/^request_U\([0-9]\{4\}\)_.*/\1/p' |
  sort -n |
  tail -n 1
)"

last_c="$(
  find "$REPORT_DIR" -type f \
    -name 'report_C[0-9][0-9][0-9][0-9]_*.md' \
    -printf '%f\n' 2>/dev/null |
  sed -n 's/^report_C\([0-9]\{4\}\)_.*/\1/p' |
  sort -n |
  tail -n 1
)"

next_u=$((10#${last_u:-0000} + 1))
next_c=$((10#${last_c:-0000} + 1))

printf 'Next User Request : U%04d\n' "$next_u"
printf 'Next Work Report : C%04d\n' "$next_c"

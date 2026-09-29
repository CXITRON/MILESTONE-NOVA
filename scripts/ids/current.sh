#!/usr/bin/env bash
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
REQUEST_DIR="$ROOT/docs/codex/requests"
REPORT_DIR="$ROOT/docs/codex/reports"

current_u="$(
  find "$REQUEST_DIR" -type f \
    \( -name 'request_U[0-9][0-9][0-9][0-9]_[EHNL]_*.md' \
       -o -name 'request_U[0-9][0-9][0-9][0-9]_[0-9][0-9][0-9][0-9]_[0-9][0-9]_[0-9][0-9].md' \) \
    -printf '%f\n' 2>/dev/null |
  sed -n 's/^request_\(U[0-9]\{4\}\)_.*/\1/p' |
  sort -V |
  tail -n 1
)"

current_c="$(
  find "$REPORT_DIR" -type f \
    -name 'report_C[0-9][0-9][0-9][0-9]_*.md' \
    -printf '%f\n' 2>/dev/null |
  sed -n 's/^report_\(C[0-9]\{4\}\)_.*/\1/p' |
  sort -V |
  tail -n 1
)"

printf 'Current User Request : %s\n' "${current_u:-U0000}"
printf 'Current Codex Report : %s\n' "${current_c:-C0000}"

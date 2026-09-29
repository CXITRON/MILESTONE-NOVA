#!/usr/bin/env bash
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"

REQUEST_ROOT="$ROOT/docs/codex/requests"
ACTIVE_DIR="$REQUEST_ROOT/active"
TEMPLATE="$ROOT/docs/codex/templates/request_template.md"

mkdir -p "$ACTIVE_DIR"

if [[ ! -f "$TEMPLATE" ]]; then
    echo "Error: Request template not found:"
    echo "  $TEMPLATE"
    exit 1
fi

if (( $# != 0 )); then
    echo "Usage: $0"
    echo "Priority is selected interactively after execution."
    exit 1
fi

echo
echo "Select Request Priority"
echo
echo "  E : Emergency  - 긴급"
echo "  H : High       - 높음"
echo "  N : Normal     - 보통"
echo "  L : Low        - 낮음"
echo

while true; do
    printf "Priority [E/H/N/L]: "
    IFS= read -r priority

    priority="$(
        printf '%s' "$priority" |
        tr -d '[:space:]' |
        tr '[:lower:]' '[:upper:]'
    )"

    case "$priority" in
        E)
            priority_name="Emergency"
            break
            ;;
        H)
            priority_name="High"
            break
            ;;
        N)
            priority_name="Normal"
            break
            ;;
        L)
            priority_name="Low"
            break
            ;;
        *)
            echo
            echo "Invalid input."
            echo "Enter exactly one of: E, H, N, L"
            echo
            ;;
    esac
done

# 모든 requests 하위 디렉토리를 확인한다.
# active/closed 여부와 관계없이 이미 사용한 U ID를 다시 발급하지 않는다.
# priority 도입 이전 legacy request_U####_YYYY_MM_DD.md도 ID 계산에 포함한다.
last_id="$(
    find "$REQUEST_ROOT" -type f \
        \( -name 'request_U[0-9][0-9][0-9][0-9]_[EHNL]_*.md' \
           -o -name 'request_U[0-9][0-9][0-9][0-9]_[0-9][0-9][0-9][0-9]_[0-9][0-9]_[0-9][0-9].md' \) \
        -printf '%f\n' 2>/dev/null |
    sed -n 's/^request_U\([0-9]\{4\}\)_.*/\1/p' |
    sort -n |
    tail -n 1
)"

next_num=$((10#${last_id:-0000} + 1))

if (( next_num > 9999 )); then
    echo "Error: U ID range exhausted."
    exit 1
fi

printf -v request_id 'U%04d' "$next_num"

file_date="$(date +%Y_%m_%d)"
doc_date="$(date +%Y-%m-%d)"

filename="request_${request_id}_${priority}_${file_date}.md"
destination="$ACTIVE_DIR/$filename"

if [[ -e "$destination" ]]; then
    echo "Error: File already exists:"
    echo "  $destination"
    exit 1
fi

sed \
    -e "s/U####/$request_id/g" \
    -e "s/\*\*Priority:\*\* P/**Priority:** $priority/g" \
    -e "s/YYYY-MM-DD/$doc_date/g" \
    "$TEMPLATE" > "$destination"

echo
echo "Request Created"
echo "────────────────────────────────────────"
echo "ID       : $request_id"
echo "Priority : $priority ($priority_name)"
echo "Date     : $doc_date"
echo "File     : ${destination#$ROOT/}"
echo "────────────────────────────────────────"
echo

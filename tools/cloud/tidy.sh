#!/usr/bin/env bash
# clang-tidy over src/libs and src/apps with the curated checks in .clang-tidy.
#   tools/cloud/tidy.sh                 # whole tree, summary by check
#   tools/cloud/tidy.sh path/file.cpp   # just these files
# Output: build-tidy/tidy.log (raw) and a count per check on stdout.
# Needs a configured build dir with compile_commands.json (created if missing).
set -euo pipefail
cd "$(dirname "$0")/../.."
BUILD="${BUILD_DIR:-build-linux}"
TIDY="${CLANG_TIDY:-$(command -v clang-tidy || command -v clang-tidy-18)}"
[ -x "$TIDY" ] || { echo "clang-tidy not found (apt install clang-tidy)" >&2; exit 1; }
if [ ! -f "$BUILD/compile_commands.json" ]; then
    cmake -S . -B "$BUILD" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >/dev/null
fi
OUT=build-tidy; mkdir -p "$OUT"

if [ $# -gt 0 ]; then
    files=("$@")
else
    mapfile -t files < <(git ls-files 'src/libs/*.cpp' 'src/apps/*.cpp')
fi

printf '%s\n' "${files[@]}" | xargs -P "$(nproc)" -n 4 \
    "$TIDY" -p "$BUILD" --quiet 2>/dev/null > "$OUT/tidy.log" || true

echo "findings by check (unique file:line):"
grep -E 'warning: .*\[[a-z0-9.,-]+\]$' "$OUT/tidy.log" | sort -u \
    | sed -E 's/.*\[([a-z0-9.,-]+)\]$/\1/' | sort | uniq -c | sort -rn
echo "raw log: $OUT/tidy.log"

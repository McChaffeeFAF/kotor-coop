#!/usr/bin/env bash
# Build and run the unit tests under a sanitizer.
#   tools/cloud/sanitize.sh [asan|ubsan|tsan] [extra gtest args]
# "asan" builds AddressSanitizer + UBSan together (the default); "ubsan" is UBSan
# alone; "tsan" is ThreadSanitizer. Each mode uses its own build dir (build-<mode>).
# Env: JOBS, SDL3_PREFIX. Dependencies come from tools/cloud/linux_test.sh
# (run it once, or set up the same packages yourself).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MODE="${1:-asan}"; [ $# -gt 0 ] && shift
JOBS="${JOBS:-$(nproc)}"
SDL3_PREFIX="${SDL3_PREFIX:-/opt/sdl3}"
SUPP="$ROOT/tools/cloud/sanitizers"

case "$MODE" in
    asan)  FLAGS="-DREONE_ASAN=ON -DREONE_UBSAN=ON" ;;
    ubsan) FLAGS="-DREONE_UBSAN=ON" ;;
    tsan)  FLAGS="-DREONE_TSAN=ON" ;;
    *) echo "usage: $0 [asan|ubsan|tsan] [gtest args]" >&2; exit 2 ;;
esac
BUILD_DIR="${BUILD_DIR:-$ROOT/build-$MODE}"

export PKG_CONFIG_PATH="$SDL3_PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export LD_LIBRARY_PATH="$SDL3_PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# Sanitized debug binaries are huge; GNU ld can be OOM-killed linking them.
LINKER=""
command -v ld.lld >/dev/null && LINKER="-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld"

cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja $LINKER -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O1 ${SAN_G:--g}" \
    -DBUILD_LAUNCHER=OFF -DBUILD_TOOLKIT=OFF -DBUILD_DATAMINER=OFF \
    -DBUILD_GFF2JSON=OFF -DBUILD_UNERF=OFF -DBUILD_NCSRUN=OFF -DENABLE_MOVIE=OFF $FLAGS
cmake --build "$BUILD_DIR" --target tests -j"$JOBS"

export ASAN_OPTIONS="detect_leaks=1:abort_on_error=0:suppressions=$SUPP/asan.supp"
export LSAN_OPTIONS="suppressions=$SUPP/lsan.supp"
# UBSan reports and continues so one run lists every finding; the run fails below.
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=0:suppressions=$SUPP/ubsan.supp"
export TSAN_OPTIONS="halt_on_error=0:second_deadlock_stack=1:suppressions=$SUPP/tsan.supp"

LOG="$BUILD_DIR/sanitizer-run.log"
rc=0
"$BUILD_DIR/bin/tests" --gtest_brief=1 "$@" 2>&1 | tee "$LOG" || rc=${PIPESTATUS[0]}
if grep -qaE "runtime error:|WARNING: ThreadSanitizer|ERROR: (Address|Leak)Sanitizer" "$LOG"; then
    echo "sanitizer findings:" >&2
    grep -aE "runtime error:|WARNING: ThreadSanitizer|ERROR: (Address|Leak)Sanitizer|SUMMARY:" "$LOG" | sort | uniq -c | sort -rn | head -40 >&2
    [ "$rc" -ne 0 ] || rc=1
fi
exit "$rc"

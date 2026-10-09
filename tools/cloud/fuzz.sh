#!/usr/bin/env bash
# Build and run one libFuzzer target for 'seconds' (default 600).
#   tools/cloud/fuzz.sh <target> [seconds] [extra libFuzzer args]
#   tools/cloud/fuzz.sh list
# Targets: key bif erf rim gff 2da tlk ssf lip lyt vis mdl tpc tga txi bwm ncs net
# ("net" is the co-op wire format in src/libs/net)
# Corpus (seeded from synthetic inputs, never game files) lives in
# $BUILD_DIR/fuzz-corpus/<target>; crashes are saved to $BUILD_DIR/fuzz-crashes/<target>/.
# FUZZ_SKIP_BUILD=1 reuses an existing build (lets several targets run in parallel).
# Needs clang and libclang-rt-dev (apt); SDL3 as in tools/cloud/linux_test.sh.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-fuzz}"
JOBS="${JOBS:-$(nproc)}"
SDL3_PREFIX="${SDL3_PREFIX:-/opt/sdl3}"
FORMATS="key bif erf rim gff 2da tlk ssf lip lyt vis mdl tpc tga txi bwm ncs net"

if [ "${1:-}" = "list" ] || [ $# -lt 1 ]; then
    echo "usage: $0 <target|list> [seconds] [libFuzzer args]"; echo "targets: $FORMATS"; exit 2
fi
TARGET="$1"; SECONDS_TO_RUN="${2:-600}"; [ $# -gt 2 ] && shift 2 || shift $#
case " $FORMATS " in *" $TARGET "*) ;; *) echo "unknown target $TARGET (targets: $FORMATS)" >&2; exit 2 ;; esac

export PKG_CONFIG_PATH="$SDL3_PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export LD_LIBRARY_PATH="$SDL3_PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

if [ -z "${FUZZ_SKIP_BUILD:-}" ]; then
LINKER=""; command -v ld.lld >/dev/null && LINKER="-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld"
CC=clang CXX=clang++ cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O1 -g1" $LINKER \
    -DREONE_FUZZ=ON -DBUILD_TESTS=OFF -DBUILD_LAUNCHER=OFF -DBUILD_TOOLKIT=OFF -DBUILD_DATAMINER=OFF \
    -DBUILD_GFF2JSON=OFF -DBUILD_UNERF=OFF -DBUILD_NCSRUN=OFF -DENABLE_MOVIE=OFF
cmake --build "$BUILD_DIR" --target "fuzz_$TARGET" fuzz_seedgen fuzz_netseed -j"$JOBS"
fi

CORPUS="$BUILD_DIR/fuzz-corpus"
if [ ! -d "$CORPUS/$TARGET" ]; then
    if [ "$TARGET" = "net" ]; then "$BUILD_DIR/fuzz/fuzz_netseed" "$CORPUS"; else "$BUILD_DIR/fuzz/fuzz_seedgen" "$CORPUS"; fi
fi
mkdir -p "$CORPUS/$TARGET" "$BUILD_DIR/fuzz-crashes/$TARGET"

export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:allocator_may_return_null=0:malloc_context_size=20}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1}"
# 1 MiB inputs, 2 GiB RSS and a 5 s per-input timeout: a hang, or an allocation
# sized from a length field, is reported as a finding.
exec "$BUILD_DIR/fuzz/fuzz_$TARGET" "$CORPUS/$TARGET" \
    -max_len=1048576 -rss_limit_mb=2048 -malloc_limit_mb=1024 -timeout=5 \
    -max_total_time="$SECONDS_TO_RUN" -print_final_stats=1 \
    -artifact_prefix="$BUILD_DIR/fuzz-crashes/$TARGET/" "$@"

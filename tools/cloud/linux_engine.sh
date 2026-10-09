#!/usr/bin/env bash
# Build the full engine on Linux (every app, movies and co-op networking), run
# the unit tests and the launcher checks, check the binary starts, and make the
# Linux tester package.
#   tools/cloud/linux_engine.sh [--release NAME] [--server HOST:PORT] [--no-package]
# Env: BUILD_DIR (default build-linux), JOBS, SKIP_DEPS=1; see linux_deps.sh.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-linux}"
PACKAGE=1
PACKAGE_ARGS=()
while [ $# -gt 0 ]; do
    case "$1" in
        --no-package) PACKAGE=0; shift ;;
        --release | --server) PACKAGE_ARGS+=("$1" "$2"); shift 2 ;;
        *) echo "unknown option $1" >&2; exit 2 ;;
    esac
done
# shellcheck source=linux_deps.sh
. "$ROOT/tools/cloud/linux_deps.sh"

cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" -j"$JOBS"

# The engine starts headless and says what it is.
"$BUILD_DIR/bin/engine" --help >/dev/null
"$BUILD_DIR/bin/engine" --help | grep -q -- '--host' || { echo "engine --help does not list --host" >&2; exit 1; }

"$ROOT/tools/package/linux/test_launcher.sh"
"$BUILD_DIR/bin/tests" --gtest_brief=1

if [ "$PACKAGE" -eq 1 ]; then
    "$ROOT/tools/package/make_linux_package.sh" --bin "$BUILD_DIR/bin" "${PACKAGE_ARGS[@]}"
fi

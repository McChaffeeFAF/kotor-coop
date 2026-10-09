#!/usr/bin/env bash
# Install dependencies, configure, build and run the unit tests on Ubuntu 24.04.
#   tools/cloud/linux_test.sh [extra gtest args]
# Env: BUILD_DIR (default build), JOBS (default nproc), SKIP_DEPS=1 to skip the
#      apt and source builds; see tools/cloud/linux_deps.sh for the rest.
# Tests need neither a display nor game data. The network transport is built in
# (GameNetworkingSockets from source), so the socket tests run instead of skipping.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
# shellcheck source=linux_deps.sh
. "$ROOT/tools/cloud/linux_deps.sh"

cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_LAUNCHER=OFF -DBUILD_TOOLKIT=OFF -DBUILD_DATAMINER=OFF \
    -DBUILD_GFF2JSON=OFF -DBUILD_UNERF=OFF -DBUILD_NCSRUN=OFF -DENABLE_MOVIE=OFF
cmake --build "$BUILD_DIR" --target tests -j"$JOBS"

exec "$BUILD_DIR/bin/tests" --gtest_brief=1 "$@"

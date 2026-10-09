#!/usr/bin/env bash
# Builds and runs the simulation benchmarks (bench/bench_sim.cpp, opt-in via
# REONE_BENCH) and writes the results as JSON.
#   tools/cloud/bench.sh                 # all benchmarks, 5 repetitions
#   tools/cloud/bench.sh coop --reps 9   # only names containing "coop"
# Output: build-bench/bench_results.json. The digest column is a hash of the
# final simulation state: it must not change when code is optimised.
set -euo pipefail
cd "$(dirname "$0")/.."
BUILD="${BUILD_DIR:-build-bench}"
JOBS="${JOBS:-$(nproc)}"

if [ ! -f "$BUILD/CMakeCache.txt" ]; then
    cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DREONE_BENCH=ON \
        -DBUILD_LAUNCHER=OFF -DBUILD_TOOLKIT=OFF -DBUILD_DATAMINER=OFF \
        -DBUILD_GFF2JSON=OFF -DBUILD_UNERF=OFF -DBUILD_NCSRUN=OFF -DENABLE_MOVIE=OFF
fi
cmake --build "$BUILD" --target bench_sim -j"$JOBS"
"$BUILD/bench/bench_sim" "$@" --json "$BUILD/bench_results.json"

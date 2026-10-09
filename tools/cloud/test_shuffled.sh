#!/usr/bin/env bash
# Runs the unit tests in random order, several times, to catch tests that depend
# on the order they run in (leaked mock expectations, static or global state).
#   tools/cloud/test_shuffled.sh [repeat] [first_seed]
# Env: BUILD_DIR (default build-linux). Build first with tools/cloud/linux_test.sh.
# A failure prints the seed: reproduce with
#   tests --gtest_shuffle --gtest_random_seed=<seed>
# and find the polluting test by bisecting --gtest_filter.
set -euo pipefail
cd "$(dirname "$0")/../.."
BUILD="${BUILD_DIR:-build-linux}"
TESTS="$BUILD/bin/tests"
[ -x "$TESTS" ] || TESTS="build/bin/tests"
[ -x "$TESTS" ] || { echo "build the tests first (tools/cloud/linux_test.sh)" >&2; exit 1; }
REPEAT="${1:-5}"
SEED="${2:-$(( $(date +%s) % 100000 + 1 ))}"
fail=0
for i in $(seq 0 $((REPEAT - 1))); do
    seed=$((SEED + i))
    if ! "$TESTS" --gtest_shuffle --gtest_random_seed="$seed" > "${TMPDIR:-/tmp}/shuffled_$seed.log" 2>&1; then
        echo "FAILED with --gtest_random_seed=$seed (log: ${TMPDIR:-/tmp}/shuffled_$seed.log)" >&2
        grep -E '^\[  FAILED  \]' "${TMPDIR:-/tmp}/shuffled_$seed.log" | sort -u | head -20 >&2
        fail=1
    else
        echo "seed $seed: ok"
    fi
done
exit $fail

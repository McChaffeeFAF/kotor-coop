#!/usr/bin/env bash
# Runs the determinism tests (world scenarios, companion-brain fight, minigame ticks, shuffled heap layouts) under conditions that expose hidden inputs:
# different heap layouts (ASLR, MALLOC_PERTURB_), repeated processes, and the
# recorded golden hashes. Optionally dumps per-tick hashes for comparison with
# another platform:  tools/cloud/determinism.sh --dump hashes.txt
set -euo pipefail
cd "$(dirname "$0")/../.."
BUILD="${BUILD_DIR:-build-linux}"
TESTS="$BUILD/bin/tests"
[ -x "$TESTS" ] || { echo "build first: tools/cloud/linux_test.sh" >&2; exit 1; }

if [ "${1:-}" = "--dump" ]; then
    KOTOR_DETERMINISM_DUMP="$(realpath -m "${2:?usage: --dump <file>}")" "$TESTS" --gtest_filter='Determinism.per_tick*'
    echo "wrote $2"
    exit 0
fi

filter='Determinism*'
fail=0
for perturb in 0 85 170; do
    for run in 1 2 3; do
        if ! MALLOC_PERTURB_=$perturb "$TESTS" --gtest_filter="$filter" >/dev/null 2>&1; then
            echo "FAIL: MALLOC_PERTURB_=$perturb run $run" >&2; fail=1
        fi
    done
done
# Test order must not matter either (state leaking from one scenario into the
# next): shuffle the order, twice.
if ! "$TESTS" --gtest_filter="$filter" --gtest_shuffle --gtest_repeat=2 >/dev/null 2>&1; then
    echo "FAIL: shuffled test order" >&2; fail=1
fi
# Dumps from separate processes must be byte-identical, with and without ASLR.
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
KOTOR_DETERMINISM_DUMP="$tmp/a" "$TESTS" --gtest_filter='Determinism.per_tick*' >/dev/null 2>&1
MALLOC_PERTURB_=99 KOTOR_DETERMINISM_DUMP="$tmp/b" "$TESTS" --gtest_filter='Determinism.per_tick*' >/dev/null 2>&1
if command -v setarch >/dev/null; then
    KOTOR_DETERMINISM_DUMP="$tmp/c" setarch "$(uname -m)" -R "$TESTS" --gtest_filter='Determinism.per_tick*' >/dev/null 2>&1 || true
fi
# The dump must cover every system: the world scenarios, the companion-brain
# fight with consumables and the minigame tick loop.
for name in crowd duels effects dice brain minigame; do
    grep -q "^$name " "$tmp/a" || { echo "FAIL: dump has no '$name' scenario" >&2; fail=1; }
done
for f in b c; do
    [ -f "$tmp/$f" ] && ! cmp -s "$tmp/a" "$tmp/$f" && { echo "FAIL: dump $f differs from a" >&2; fail=1; }
done
[ "$fail" = 0 ] && echo "determinism: ok"
exit $fail

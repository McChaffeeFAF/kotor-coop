# Determinism

Co-op guests do not simulate (see [COOP.md](COOP.md)), so a co-op session does not need
the guests' machines to compute the same results as the host. The simulation is still kept
deterministic: the same world, the same inputs and the same gameplay seed give the same
state, tick by tick, in every process and on every platform. That makes bugs
reproducible, lets a Linux host and Windows guests agree on what they compute locally
(headings, positions), and keeps the door open to replays. This page describes how,
and the tests that hold it in place.

## A fixed tick

`FixedStepClock` (`include/reone/system/fixedstep.h`) runs the simulation in fixed steps,
30 per second by default (`--tickrate`). Each rendered frame adds its wall time and runs
the whole steps that are due; the fraction left over is carried to the next frame and is
the interpolation factor the renderer draws with. A frame never runs more than a few
steps: after a long stall (a breakpoint, a slow load) the overdue time is dropped rather
than caught up. Simulation code sees only the step length, never the frame time.

## Portable maths

- **Trigonometry.** The C library's `sin`, `cos` and `atan2` differ by an ulp between
  glibc, MSVC and libc++. Simulation code uses `portable::sin`, `cos` and `atan2`
  (`include/reone/system/portablemath.h`), built from `+ - * /` and `floor` only, which
  IEEE 754 rounds exactly, so they give bit-identical results everywhere. This covers
  facing and headings, party formation, turret aim, minigame track blending, `Location`
  facing and the script trigonometry routines.
- **No fused multiply-add.** GCC and Clang may fuse `a * b + c` into one rounding; MSVC
  does not. The build passes `-ffp-contract=off` on GCC and Clang (top-level
  `CMakeLists.txt`). Keep it.
- **Random numbers.** `RandomGenerator` (`include/reone/system/randomutil.h`) is
  xoshiro128** seeded through splitmix64, with its own integer and float mappings,
  because `std::uniform_*_distribution` gives different sequences on different standard
  libraries. Dice, AI and movement picks draw from it.

## Ordered iteration

Nothing in the simulation may depend on memory addresses or hash-table order:

- iterate objects in id order (or another stable key), not over pointer-keyed or
  unordered containers;
- break ties stably (for example, of two creatures at the same distance, the lower id
  wins);
- sort with a total order (`std::stable_sort` where equal keys can occur);
- initialise every member (an uninitialised value is a hidden input).

## The tests

`test/game/determinism.cpp` builds small worlds from synthetic data (no game files) and
runs them for a number of ticks, hashing the whole state after each tick. The scenarios
are a crowd of moving creatures, duels, effects, dice rolls, a companion-brain fight and
minigame ticks.

- The same scenario twice in one process must give the same hashes (catches state leaking
  between runs, uninitialised members and address-ordered iteration).
- The same scenario with the heap allocation order shuffled must give the same hashes.
- Scenarios that roll dice must follow the seed; the rest must not depend on it.
- **The golden file.** `test/data/determinism_golden.txt` holds the hash at every 30th
  tick (each simulated second) of each scenario, as recorded on Linux. `DeterminismGolden` recomputes them and
  fails at the first difference. This is what catches drift between compilers and
  platforms: the Windows build must reproduce the Linux hashes.

When you change the simulation on purpose, the golden test fails. Regenerate the file
with `KOTOR_DETERMINISM_UPDATE=1` set while running `tests`, check that the other
determinism tests still pass, and commit the new file with the change that caused it.
`KOTOR_DETERMINISM_DUMP=<file>` writes every scenario's per-tick hashes, to compare two
machines.

`tools/cloud/determinism.sh` (Linux) runs the determinism tests under different heap
layouts (`MALLOC_PERTURB_`), repeated processes and a shuffled test order, and with
`--dump <file>` writes the per-tick hashes for a cross-platform comparison.

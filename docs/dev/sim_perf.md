# Simulation performance

A co-op host runs the whole simulation plus networking at 30 Hz: a tick has
33 ms to spend. `bench/bench_sim.cpp` (opt-in: `-DREONE_BENCH=ON`, or
`tools/cloud/bench.sh`) times synthetic scenes built from the unit-test
fixtures. No game data is involved.

Each benchmark prints the best and median wall time of N repetitions, the time
per unit of work (a tick, a script, a snapshot), and a **digest** of the final
simulation state. An optimisation must leave every digest unchanged; the
determinism tests (golden per-tick hashes) were also run against these changes
and still pass.

| benchmark | what it runs |
|---|---|
| `path_crowd_64` | 64 creatures walking across a synthetic walkmesh, 300 ticks |
| `combat_pairs_8` | 8 pistol duels, 300 ticks |
| `effects_400` | 400 creatures with a temporary and a permanent regeneration effect, 300 ticks |
| `script_loop_20k` | the VM running a 20,000-iteration counting loop, 5 times |
| `script_startup_5k` | 5,000 tiny scripts: construct a VM, run three instructions |
| `snapshot_full_300` | encode + decode of a full 300-entity co-op snapshot, 200 times |
| `snapshot_delta_300` | the same for a position/vitals delta, 1000 times |
| `coop_host_200_idle` | a host session with a connected client and 200 idle creatures, 300 ticks (world step, `beforeTick`, `afterTick`, network poll) |
| `coop_host_200_moving` | the same with all 200 creatures walking |

## Results

Release build (-O3), one machine, minimum of 5 runs, milliseconds per run.
The mocked services (`MockReputes` etc.) cost something in the co-op rows that
the real ones do not.

| benchmark | before | after | change |
|---|---:|---:|---|
| `path_crowd_64` | 35.1 | 31.3 | -11% |
| `combat_pairs_8` | 2.19 | 2.10 | within noise |
| `effects_400` | 123.7 | 44.5 | **2.8x faster** |
| `script_loop_20k` | 28.4 | 26.8 | -6% |
| `script_startup_5k` | 34.4 | 2.55 | **13x faster** |
| `snapshot_full_300` | 22.6 | 22.1 | none (not a hotspot) |
| `snapshot_delta_300` | 27.1 | 26.8 | none (not a hotspot) |
| `coop_host_200_idle` | 61.9 | 59.4 | -4% |
| `coop_host_200_moving` | 133.4 | 114.5 | -14% |

In per-tick terms, at 200 creatures the host spends about 0.2 ms idle and
0.4 ms with everyone moving: about 1% of the 33 ms budget.

## What was changed (all with unchanged digests)

1. **Script VM dispatch table** (`virtualmachine.cpp`). Each `VirtualMachine`
   built an `unordered_map` of ~110 `std::function`/`std::bind` wrappers in its
   constructor, so starting a short script cost more than running it. The table
   of member-function pointers is now built once per process. Heartbeats and
   event handlers start many tiny scripts.
2. **`Object::hasEffect`** (`object.cpp`). It is asked dozens of times per
   creature per tick (held-pose checks, `canExecuteActions`...). Each call ran
   a `dynamic_pointer_cast<LinkEffectsEffect>` on every effect, which is a
   type-name comparison and an atomic reference-count round trip when the cast
   fails. It now returns at once for an empty list, compares the effect type
   first (only the asked type or a link can answer yes), and only then checks
   liveness and the link. `flatEffects` skips the cast for effects not typed
   as links.
3. **Creature collision search** (`Area::findCreatureCollision`). Every move
   swept a circle against every creature in the area. A creature whose circle
   cannot reach the box the move sweeps is now rejected with four comparisons
   before the dead / friendly-collision checks and the sweep. The margin is
   conservative, so it never rejects what the sweep would accept.

## Looked at and left alone

- **Snapshot encode/decode**: about 27 us for a 300-entity delta, 110 us for a
  full snapshot. Inlining the byte writer and reader did not help (measured,
  reverted).
- **`Object::updateEffects`** copies every effect instance each tick because an
  `onUpdate` may apply new effects. Skipping the copy for effects without an
  `onUpdate` would save about 15% of `effects_400`, but needs a per-class flag
  that a new periodic effect could forget; not worth the trap.
- **`CoopSession::hostUpdateScaling`** asks the reputation service about every
  non-party creature each tick while more than one player is in the session
  (the mock makes this look worse than it is), and `hostCaptureEntities`
  rebuilds the entity map each tick. Both are linear in the creature count.
- **`Creature::computeCreatureAvoidance`** scans all creatures per walking
  creature per tick (quadratic overall); it would profit from the same
  broad-phase as the collision search, together with a spatial index for
  `Area`, which is a larger change.

## Profiling without perf

`perf` is not available in the cloud image. Use callgrind on a single
benchmark:

    valgrind --tool=callgrind --callgrind-out-file=out build-bench/bench/bench_sim effects --reps 1
    callgrind_annotate out | head -40

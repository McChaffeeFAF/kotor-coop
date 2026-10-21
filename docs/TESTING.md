# Testing

## Unit tests

`tests` (built by default, `BUILD_TESTS=ON`) is a GoogleTest binary with several
thousand tests. They use synthetic data only, built in the test or written out as text:
no game files, no display and no network are needed.

```
build/bin/tests                                  # everything
build/bin/tests --gtest_brief=1                  # print only failures
build/bin/tests --gtest_filter='Coop*'           # one area
tools/cloud/linux_test.sh                        # Linux: deps, build and run (see BUILDING.md)
```

A handful of tests skip themselves when an optional piece (for example the network
transport) is not built in.

Where they live: `test/<library>/` mirrors `src/libs/<library>/`. Useful pieces:

- `test/fixtures/`: mocks and builders for the engine's services, so a `Game` can run
  without an install.
- `test/support/nsscomp.h`: a small NWScript compiler for tests, so a test can write a
  script as a string and run it on the engine's VM ([testing_scripts.md](testing_scripts.md)).
- `test/game/coopnet.cpp` and the other `test/game/coop*.cpp`: a host and guests in one
  process over the loopback transport ([COOP.md](COOP.md)).
- `test/net/`: the wire format, round trips and the join-code link against a fake
  service on loopback sockets ([protocol_coverage.md](protocol_coverage.md)).
- `test/game/determinism.cpp` and `test/data/determinism_golden.txt`: see
  [DETERMINISM.md](DETERMINISM.md).

Add a test with every change, run the whole suite before a pull request, and keep tests
free of game data: write the 2DA rows, GFF fields or scripts a test needs into the test.

Other test tooling, all opt-in: sanitizer builds (`REONE_ASAN`, `REONE_UBSAN`,
`REONE_TSAN`; `tools/cloud/sanitize.sh`), libFuzzer targets for the file formats and the
network protocol (`REONE_FUZZ`, `fuzz/`, `tools/cloud/fuzz.sh`), simulation benchmarks
(`REONE_BENCH`, `bench/`, [sim_perf.md](sim_perf.md)) and the determinism runs
(`tools/cloud/determinism.sh`).

## The headless harness (control port)

The engine can be driven by a script, for testing against a real game install:

```
engine --game <KotOR folder> --headless --control-port 5555
```

- `--headless` never shows the window.
- `--control-port <port>` listens on **127.0.0.1 only**, takes one client at a time, and
  reads one console command per line. Commands run on the main thread, between frames.
  Each reply is one JSON line:
  `{"id":n,"cmd":"...","ok":true,"output":["..."],"frame":n,"time":s,"tick":n}`.

The commands are the engine's console commands (`help` lists them). They queue the same
actions mouse clicks and scripts queue, so pathfinding and interaction behave as in play.
Some of them: `newgame`, `loadgame`, `savegame`, `warp`, `moveto`, `movetoobject`,
`click`, `opendoor`, `startconversation`, `pickreply`, `takeall`, `equipslot`,
`levelup`, `waitidle`, `step`, `untiltick`, `tickstatus`, `dumpstate`, `dumpobject`,
`guidump`, `guiclick`, `mgstate`, `seed`. Co-op adds `coophost`, `coopjoin`,
`netstatus` and the `coop...` commands that act as a guest would (`coopmove`,
`coopattack`, `coopuse`, `coopsay`, ...).

`seed <n>` reseeds the gameplay random stream, so a scripted run can be repeated.
`--coop-sim-lag`, `--coop-sim-loss` and `--coop-sim-jitter` add network trouble, and
`--coop-record <file>` writes the game and co-op state once a second.

**The integration probes are not included.** During development, scripts that drive a
real install through this port were used to test the game end to end (story
progression, co-op sessions, screenshots of the interface). They depend on the
developer's own game installs and are not part of this repository; the docs mention them
only as "an integration probe (not included)". The harness itself is complete: any
language that can open a TCP socket to 127.0.0.1 can drive it.

The control port has no authentication. It is meant for test machines; do not expose it
beyond localhost (it only binds to 127.0.0.1).

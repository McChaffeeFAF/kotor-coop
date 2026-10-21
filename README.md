# KotOR co-op engine (a reone fork)

**Based on [reone](https://github.com/seedhartha/reone) by seedhartha and contributors. Licence: GPL-3.0.**

An open-source engine for Star Wars: Knights of the Old Republic that adds online co-op
to the KotOR campaign. It is a fork of reone, the free and open-source re-implementation
of the Odyssey engine: the game logic, rendering and interface run in this code, and the
game's data (models, textures, scripts, dialogue) is read from your own installs. Friends
can take over your companions (Story Mode) or bring their own characters (Free Mode).
Developers can build on it: see [docs/BUILDING.md](docs/BUILDING.md),
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and [CONTRIBUTING.md](CONTRIBUTING.md).

## Features

- **Host-authoritative co-op.** The host runs the game; guests send commands and draw
  what the host sends. Story Mode takes up to 3 players (the host and two guests playing
  companions), Free Mode up to 8, each guest with their own character.
- **Connect by IP:port** over UDP (port 27015 by default); no account or server needed.
- **Co-op chat and pings**, shown on the K1 and K2 HUDs.
- **Dedicated host**: `engine --dedicated` runs a Free Mode session with no local player
  ([docs/dedicated_host.md](docs/dedicated_host.md)).
- **K2 systems in K1**: with a KotOR II install, KotOR II's feats, Force powers and
  prestige classes are merged into the KotOR campaign, read at run time from your install.
- **Enhanced graphics**, each effect switchable: bloom, soft shadows, ambient occlusion,
  tone mapping, reflections and locally generated Surface Detail maps. A fresh install
  starts with a tuned selection of them on; the original look is one setting away.
- **Headless test harness**: unit tests with synthetic data, a fixed-step deterministic
  simulation, and a localhost control port for scripted runs ([docs/TESTING.md](docs/TESTING.md)).
- **Linux and Steam Deck**: finds Steam (including extra libraries and Flatpak), GOG,
  Heroic and Lutris installs, with Steam Deck screen defaults.

## This is a fan project

This is an unofficial, non-commercial fan project. It is not affiliated with,
endorsed by or connected to Lucasfilm, Disney, BioWare, Aspyr or Electronic Arts.
Star Wars, Knights of the Old Republic and related names are trademarks of their
respective owners.

**You need your own, legally obtained copies of Knights of the Old Republic and
Knights of the Old Republic II.** This repository contains no game files, and none
are ever downloaded: the engine reads the data files from your own installs and
never changes them. KotOR II's install is used for its interface and systems.


**Target platform:** this project is intended for the **Steam versions** of Star Wars:
Knights of the Old Republic and Knights of the Old Republic II. Other versions (GOG,
retail discs) may work but are untested.

## How this was made

The engine's behaviour was matched using public file-format documentation, observation
of the original games, and clean-room research notes. No original game code or assets
are included or distributed. This is an unofficial fan project.

## Building

The build uses CMake (3.16 or newer) and a C++17 compiler. The executables land in
`build/bin`: `engine` is the game, `texgen` the enhanced-texture generator and
`tests` the unit tests.

### Windows

Visual Studio 2022 or newer with the C++ workload, and
[vcpkg](https://github.com/microsoft/vcpkg) for the libraries:

```
vcpkg install --triplet x64-windows boost-program-options boost-json boost-format ^
    boost-algorithm boost-tokenizer sdl3 gtest libmad openal-soft ^
    ffmpeg gamenetworkingsockets openssl protobuf
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake ^
    -DBUILD_TOOLKIT=OFF -DBUILD_LAUNCHER=OFF
cmake --build build --config Release --parallel
```

The toolkit and the old launcher need wxWidgets; turn them on only if you have it.

### Linux

On Ubuntu 24.04, `tools/cloud/linux_deps.sh` installs the packages and builds the
two libraries Ubuntu does not package (SDL3 and GameNetworkingSockets). Then:

```
source tools/cloud/linux_deps.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TOOLKIT=OFF -DBUILD_LAUNCHER=OFF
cmake --build build
```

Other distributions need the same libraries: Boost, SDL3, GoogleTest, libmad, OpenAL,
FFmpeg and GameNetworkingSockets (with OpenSSL and Protobuf).

### Tests

Run `tests` from `build/bin` (with Visual Studio, `build\bin\Release` or `build\bin`,
depending on the generator). The unit tests use synthetic data only and need no game
files. The integration probes used during development drive a real game install and are
not part of this repository; comments that mention a "design note" refer to development
notes that are not included either.

## Playing

### The launcher

`tools/package/make_tester_package.ps1` (Windows) and `tools/package/make_linux_package.sh`
(Linux) build a portable folder with the engine and its libraries, and no game files.
In it, run **Play KotOR Co-op.cmd** (Windows) or **./play.sh** (Linux). The first time it
finds your KotOR and KotOR II installs (Steam first, GOG too); if it can't, pick the folders
yourself. The Steam versions are the intended target; other versions (GOG, retail discs)
may work but are untested. Then choose Host, Join or Play solo.

You can also run the engine directly:

```
engine --game <KotOR folder> --k2game <KotOR II folder>
```

### Host and join by IP:port

Co-op connects players directly over UDP.

- **Host:** click *Host a co-op game* (or run `engine --host`), then start a new game or
  load a save. You listen on **UDP port 27015** (`--host <port>` picks another).
  Friends outside your home network need that port forwarded: **forward UDP 27015 on
  your router to your PC**, then share your public IP address and the port, for example
  `203.0.113.7:27015`. Allow the engine through your firewall the first time.
- **Join:** type the host's IP:port into the launcher and click *Join by IP:port*, or use
  *Co-op > Join by IP:port* on the main menu (or `engine --join 203.0.113.7:27015`). A
  bare IP or host name (launcher, Co-op screen and `--join`) gets port 27015.

When someone connects, the host gets an Accept / Decline prompt. Players on the same
network can use the host's local IP and need no forwarding.

An optional join-code service is in `tools/coopserver` for anyone who wants to run their
own (`coop-server=your.server:27200` in reone.cfg, or `-Server` / `--server` when
packaging). With it, the host gets a short join code instead and no port forwarding is
needed. Builds do not use one unless it is configured.

### Enhanced graphics

*Options > Graphics > Enhanced Graphics* (or `enhanced=1` in reone.cfg) turns on bloom, SMAA, soft shadows, ambient occlusion, tone mapping, eye
adaptation, soft particles, reflections, lightsaber glow, conversation depth of field and
Surface Detail. Each effect can also be toggled on its own (*Options > Graphics >
Advanced*). A fresh install, with no saved settings, starts with a tuned selection of
these effects on; setting the Enhanced Graphics box to off (the Retail preset) gives the
original look, and the options pages' Default button returns to the tuned selection.

### Surface Detail

Surface Detail adds normal and material maps that are generated on your own PC from your
own KotOR textures. Click *Generate enhanced textures* in the launcher, or run:

```
texgen --game <KotOR folder> --out enhanced_textures
```

The maps land in `enhanced_textures` beside the engine, which picks them up the next time
you play. They are derived from the game's textures, so they stay on your PC: never
commit, upload or share them. See [docs/texgen.md](docs/texgen.md).

## Licence and credits

GPL-3.0: see [COPYING](COPYING). Source files keep their copyright headers.

This project builds on the work of:

- **reone** by seedhartha and its [contributors](CONTRIBUTORS), the engine this fork
  is based on;
- the **modawan** fork, whose commits form a large part of this tree;
- work that came in through forks and ports, credited in [CONTRIBUTORS](CONTRIBUTORS) and
  in the original commit messages.

Thank you to all of them.

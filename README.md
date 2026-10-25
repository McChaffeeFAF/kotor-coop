# KotOR Co-op

Play Star Wars: Knights of the Old Republic's campaign online with friends. KotOR Co-op
is a fan-made, open-source engine based on [reone](https://github.com/seedhartha/reone)
(GPL-3.0). It plays the game from your own installed copy and adds host-authoritative
co-op: friends join your game by IP address and take over your companions, or bring
characters of their own.

## Download and play

1. Download the latest release from
   **[Releases](https://github.com/McChaffeeFAF/kotor-coop/releases)** and unzip it
   anywhere (not inside your game folder).
2. Double-click **Play KotOR Co-op.cmd**. The first time, it finds your game installs by
   itself (Steam, including libraries on other drives, and GOG). If it can't, click
   *Change...* and pick the folder that has `swkotor.exe`.
3. Choose *Play solo*, *Host a co-op game* or *Join by IP:port*.

The first time, Windows may warn that `engine.exe` is an unrecognised app (it is not
code-signed): click *More info*, then *Run anyway*. Allow it through Windows Firewall
when asked, or co-op cannot connect. On Linux, run `./play.sh` from the Linux package
instead.

**What you need**

- **Your own copy of Star Wars: Knights of the Old Republic**, ideally the Steam version
  (other versions such as GOG may work but are untested). No game files are included or
  downloaded; the engine reads your install and never changes it.
- **Knights of the Old Republic II is optional.** If you have it installed, the engine
  can use its interface look, and merges KotOR II's feats, Force powers and classes into
  the KotOR campaign.

## Host and join by IP

- **Host:** click *Host a co-op game*, then start a new game or load a save. You host on
  **UDP port 27015**. For friends outside your home network, **forward UDP 27015** on
  your router to your PC and allow `engine.exe` through your firewall. Then send them
  your **public IP** address and the port, for example `203.0.113.7:27015`.
- **Join:** type the host's IP:port into the launcher and click *Join by IP:port*. An
  address without a port uses 27015.
- **Same network (LAN):** use the host's local IP address (for example `192.168.1.10`);
  no port forwarding is needed.
- When someone connects, the host gets an **Accept / Decline** prompt.

**Story Mode** takes up to 3 players: the host plays the main character and friends take
over companions. **Free Mode** takes up to 8 players, each with their own character.

## Known issues

This is an alpha. Known problems in v0.5.0:

- **Endar Spire, first room door with a guest playing Trask:** if a guest controls Trask,
  the door out of the first room may not open. Workaround: the guest leaves the session,
  the host takes control of Trask and opens the door, then the guest rejoins.
- **KotOR II tables for guest stand-ins** aren't written yet: in a KotOR II game a guest
  with no free companion plays a copy of the host's character.
- **Free Mode map:** a guest exploring a different area from the host doesn't reveal that
  area's map.
- **Gamepad support** is lightly tested.
- All players must run the same version (network protocol 14).

## Reporting bugs

- Open an issue on **[GitHub Issues](https://github.com/McChaffeeFAF/kotor-coop/issues)**
  with the *Bug report* template.
- Right after the problem happens, click *Report a bug* in the launcher (or run
  **ReportBug.cmd**; `./report-bug.sh` on Linux). It packs the game's log, the co-op
  recording and your settings (no game files, no saves) into the `reports` folder.
  Attach that file to the issue.
- Questions and ideas: use
  **[Discussions](https://github.com/McChaffeeFAF/kotor-coop/discussions)**.

## Features

- **Host-authoritative co-op**: the host runs the game, and guests send what they do and
  see the host's world. Story Mode up to 3 players, Free Mode up to 8.
- **Connect by IP:port** over UDP; no account or server needed.
- **Co-op chat and pings.**
- **Dedicated host**: `engine --dedicated` runs a Free Mode session with no local player
  ([docs/dedicated_host.md](docs/dedicated_host.md)).
- **KotOR II systems in KotOR** (optional, with a KotOR II install): KotOR II's feats,
  Force powers and prestige classes in the KotOR campaign.
- **Enhanced graphics**: bloom, soft shadows, ambient occlusion, tone mapping,
  reflections and more, each effect switchable in *Options > Graphics*. A fresh install
  starts with a tuned selection on, and the Retail preset gives the original look.
  *Surface Detail* maps can be generated on your PC from your own textures
  ([docs/texgen.md](docs/texgen.md)).
- **Gamepad support** with rebinding ([docs/gamepad.md](docs/gamepad.md)).
- **Linux and Steam Deck**: finds Steam (including Flatpak), GOG, Heroic and Lutris
  installs.

## Building from source

CMake and a C++17 compiler on Windows (Visual Studio and vcpkg) or Linux. See
**[docs/BUILDING.md](docs/BUILDING.md)**. The unit tests need no game files
([docs/TESTING.md](docs/TESTING.md)). How the engine and co-op are put together:
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and [docs/COOP.md](docs/COOP.md).
Contributions are welcome: see [CONTRIBUTING.md](CONTRIBUTING.md).

## Credits

This project builds on the work of:

- **reone** by seedhartha and its contributors, the engine this project is based on;
- the **modawan** fork, whose commits form a large part of this tree;
- the forks and ports credited in [CONTRIBUTORS](CONTRIBUTORS);
- contributors to this project, including **BruhfessorX** (packaging fixes).

Thank you to all of them. The full list is in [CONTRIBUTORS](CONTRIBUTORS).

## This is a fan project

This is an unofficial, non-commercial fan project. It is not affiliated with, endorsed
by or connected to Lucasfilm, Disney, BioWare, Aspyr or Electronic Arts. Star Wars,
Knights of the Old Republic and related names are trademarks of their respective
owners. You need your own, legally obtained copy of the game. This repository contains
no game files.

Licence: GPL-3.0, see [COPYING](COPYING). Source files keep their copyright headers.

## How this was made

The engine's behaviour was matched using public file-format documentation, observation
of the original games, and clean-room research notes. No original game code or assets
are included or distributed.

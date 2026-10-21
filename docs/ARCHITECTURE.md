# Architecture

Where things live, for developers new to the tree. The layout is upstream reone's; this
fork mostly adds files beside the existing ones. Headers are in `include/reone/<lib>/`,
sources in `src/libs/<lib>/`, tests in `test/<lib>/`.

## Programs (`src/apps/`)

- `engine/`: the game. `main.cpp` parses options (`optionsparser.cpp`; `engine --help`
  lists them), sets up logging and runs `Engine` (`engine.cpp`). `Engine` builds the
  services, chooses the game (K1 or K2) from the install, mounts the optional K2 install,
  creates `game::Game`, starts the localhost control channel (`controlchannel.cpp`, see
  [TESTING.md](TESTING.md)), and starts co-op when `--host`, `--join` or `--dedicated`
  asks for it.
- `texgen/`: the Surface Detail map generator ([texgen.md](texgen.md)).
- `shaderpack/`: packs `glsl/` into `shaderpack.erf` at build time.
- `launcher/`, `toolkit/`, `dataminer/`, `gff2json/`, `unerf/`, `ncsrun/`: upstream
  reone tools.

## Libraries (`src/libs/`)

| library | what it holds |
|---|---|
| `system` | logging, streams, the fixed-step clock (`fixedstep.h`), portable math and random numbers (`portablemath.h`, `randomutil.h`), install detection (`gamedetect.h`) |
| `resource` | the game's file formats (KEY/BIF, ERF/RIM, GFF, 2DA, TLK, TPC, ...), resource lookup over an install, and the K2 mounts (`k2merge.h`, `presentationresources.h`) |
| `graphics` | OpenGL context, textures, models, walkmeshes, shaders, the graphics presets (`preset.cpp`), Surface Detail loading |
| `scene` | the scene graph (models, emitters, lights, cameras) and the render pipelines (`scene/render/`) |
| `audio`, `movie` | sound (OpenAL) and movie playback (FFmpeg) |
| `gui` | the GUI system: controls, `.gui` layouts read from the game's data, gamepad menu navigation |
| `input` | input events |
| `script` | the NWScript virtual machine (NCS bytecode) |
| `game` | the game itself: objects, actions, combat and d20 rules, effects, party, saves, GUI screens, the routine implementations scripts call, and co-op |
| `net` | the co-op wire protocol and transports |
| `texgen` | the Surface Detail generator's library |
| `tools`, `json` | upstream tool support (script decompiler for mod scripts, GFF to JSON) |

## The game library (`src/libs/game/`)

- `game.cpp` / `game.h`: `Game`, the top-level object. It owns the module, the party,
  the screens and the simulation clock; `Game::update` runs fixed simulation ticks and
  draws with interpolation between them.
- `object/`: creatures, doors, placeables, triggers, areas and modules. `action/`: the
  action queue entries (move, attack, cast, use, ...).
- `combat.cpp`, `attack.cpp`, `d20/`: combat rounds, attack rolls, feats, spells
  (Force powers) and classes.
- `script/routine/impl/`: the NWScript routines the engine implements
  ([routines_missing.md](routines_missing.md) lists the ones still missing).
- `gui/`: the game's screens (HUD, main menu, conversation, character, inventory,
  options, workbench, ...).
- `savegame.cpp`, `savedgame.cpp`, `savedruntime.cpp`: reading and writing the game's
  own save format.

### Effects

Each game effect type is a class in `game/effect/` (damage, heal, stun, Force push,
beam, visual, ...), created by the effect routines and applied to objects. What an
effect looks like is separate: `visualeffects.cpp` reads `visualeffects.2da` rows into
descriptions (models, attach points, progfx numbers), `visualeffectpresenter.cpp` turns
them into scene nodes (attached models, emitters, beams, body overlays), and
`hitvisuals.cpp` handles hit flashes. In co-op only the presentation crosses the wire:
the host sends effect events (`Events` messages) and guests draw them.

### GUI layouts, K1 and K2

Screens are built from the game's own `.gui` layout resources, so the same code serves
both games; where the layouts differ, the screen looks up the controls that layout
actually has. `--hud k1` (the default) uses KotOR's layouts in a KotOR session.
`--hud k2` in a KotOR session reads KotOR II's GUI set from the install named by
`--k2game` (`mountK2PresentationSources`): only GUI resources, in KotOR II's own
precedence, never its modules or world textures. Game rules always follow the game
being played.

### The K2 systems merge

With `--k2game` naming a KotOR II install, a KotOR session also merges KotOR II's
feats, Force powers, classes (including the prestige classes), Force forms, crafting and the
level-50 experience table into KotOR's data (`resource/k2merge.h`,
`mountK2DataSources`; turn it off with `--k2merge 0`). The merge is built at run time
from both installs and nothing is copied or written: KotOR's table rows keep their
indices, KotOR II-only rows are appended after them, every merged table keeps a map from
KotOR II row to merged row, and string references of appended rows resolve through
KotOR II's talk table. KotOR II scripts the merge runs (named with a `k2:` prefix) use
KotOR II's routine table (`ScriptRunner::setK2Routines`), with table-row arguments such as
visual-effect rows remapped to the merged rows. Without a KotOR II install, KotOR's
data is served unchanged. Startup fails with a clear error if `--k2game` is not a KotOR II
install.

### Co-op

See [COOP.md](COOP.md) for the model. The code:

| file (`game/`) | part |
|---|---|
| `coopgame.cpp` | starting a session from the menus or the command line (`CoopLaunch`) |
| `coopsession.cpp` | the session: connections, Hello/Welcome, slots, snapshots, commands, rejoin tokens |
| `coopreplication.cpp` | reading replicated state off host objects and applying it to the client's copies |
| `coopidmap.cpp` | matching host and client objects by stable load keys, since ids differ between processes |
| `coopcommand.cpp` | validating guest commands and rate limiting |
| `coopfree.cpp`, `cooploot.cpp`, `coopinventory.cpp` | Free Mode guests' own characters, loot and inventories |
| `coopflow.cpp`, `coopflows.cpp`, `coopprompts.cpp` | the area-exit ready check, join prompts and other flows |
| `coopprogress.cpp` | checking a guest's level-up |
| `coopstandin.cpp` | stand-in characters when no companion is free |
| `coopbench.cpp`, `guestbench*.cpp` | guests at workbenches ([guest_workbench.md](guest_workbench.md)) |
| `coopmg.cpp`, `coopminigame.cpp` | guests in minigames ([guest_minigames.md](guest_minigames.md)) |
| `coopchat*.cpp`, `gui/hudchat.cpp` | text chat and pings |
| `dedicated.cpp`, `dedicatedgame.cpp`, `coopdedicated.cpp` | the dedicated host ([dedicated_host.md](dedicated_host.md)) |

In `net/`: `protocol.cpp` encodes and decodes every message, `gnstransport.cpp` is the
GameNetworkingSockets UDP transport, `loopback.cpp` an in-process transport for tests,
`fingerprint.cpp` the build and game-file fingerprints exchanged at join, and
`joincode.cpp` / `signallink.cpp` the optional join-code service client.

## Shaders and tools

- `glsl/`: the GLSL shaders, packed into `shaderpack.erf` by every build.
- `tools/package/`: player package scripts. `tools/coopserver/`: the optional join-code
  service. `tools/cloud/`: Linux build, test, fuzz, sanitizer and determinism scripts.
- `fuzz/`, `bench/`: libFuzzer targets and simulation benchmarks (opt-in CMake options).

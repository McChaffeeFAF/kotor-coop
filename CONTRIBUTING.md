# Contributing

Thank you for helping. This is a fan project built on [reone](https://github.com/seedhartha/reone);
contributions are welcome under the same licence.

## Before you open a pull request

1. **Build** the tree ([docs/BUILDING.md](docs/BUILDING.md)) on Windows or Linux.
2. **Run the unit tests** (`build/bin/tests`, or `tools/cloud/linux_test.sh` on Linux).
   They must all pass. Add tests for what you change; they must use synthetic data only.
3. If you touch the simulation and the determinism golden test fails on purpose,
   regenerate `test/data/determinism_golden.txt` as described in
   [docs/DETERMINISM.md](docs/DETERMINISM.md) and say why in the pull request.
4. If you change how a co-op message is encoded, bump `kProtocolVersion` in
   `include/reone/net/protocol.h` and note it beside the constant
   ([docs/COOP.md](docs/COOP.md)).
5. Keep pull requests focused, and describe what you tested.

New here? Start with [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## The clean-room rule

This project must stay free of anything it may not distribute. Pull requests that break
these rules cannot be merged:

- **No game assets.** No files from the games or derived from them: models, textures,
  sounds, music, movies, 2DA/GFF/TLK/dialogue data, scripts, save games, or generated
  maps (texgen output). No screenshots or recordings that show game art. Tests write the
  little data they need themselves.
- **No decompiled or disassembled code** from the original games' executables, and no
  code, structure layouts, names or memory addresses taken from them. Do not paste output
  of decompilers or debuggers attached to the original games.
- What is fine: public file-format documentation, behaviour you observe by playing, the
  games' data formats and NWScript's public routine names, and other GPL-compatible open
  source work (credit it in the commit message and, for a new source, in
  [CONTRIBUTORS](CONTRIBUTORS)).

## Licence

The project is GPL-3.0 ([COPYING](COPYING)). By contributing, you agree that your
contribution is licensed under GPL-3.0 as well. Keep the copyright headers on existing
files, and put the same header on new ones.

## Reporting bugs

Open an issue with the **Bug report** template. Please include:

- the build (release name such as `alpha-0.4.4`, or the commit for a source build);
- KotOR or KotOR II, and Steam, GOG or another version;
- solo or co-op (host or guest, Story or Free Mode);
- the steps that lead to the problem;
- the log: `engine.log` in the KotOR Co-op folder (the player package folder, not your
  KotOR install). In a player package, **ReportBug.cmd**
  (Windows) or **./report-bug.sh** (Linux) zips the log, the co-op recordings and your
  settings into an archive in the `reports` folder; no game files or saves go in.

Do not attach game files or saves that contain game data.

Ideas and requests are welcome too: use the **Feature request** template.

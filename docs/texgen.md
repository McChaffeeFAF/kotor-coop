# texgen: enhanced-texture generator

`texgen` reads **your own** KotOR / KotOR II textures and writes generated **normal**
and **material** maps to a local folder the engine can load (the engine side is built
separately). Run it on your own PC, on your own install.

## The never-ship rule

The maps are derived from game assets. They are therefore:

* **never committed** to the repository, in any branch;
* **never shipped**: not in the player package, not in an installer, not in a release;
* **never shared**.

The tool itself is built with the engine (`-DBUILD_TEXGEN=ON`, the default) but the
player packages contain the engine only. The Linux package script adds `bin/texgen` only
on request (`--with-texgen`), and refuses to build a package that holds texgen output
(`*_n.tga`, `*_m.tga`, `*_n.dds`, `*_m.dds`, `texgen_manifest.json`) whatever the flags are. Tests use
synthetic textures only; no game data is in the repository.

## Usage

```
texgen --game <install dir | chitin.key | .erf | .mod | .rim | .tga | .tpc> --out <folder> [options]

  --rules FILE      JSON rules file (default: built-in naming-convention rules)
  --strength N      base normal map strength (default 2.0)
  --threads N       worker threads (default: one per core)
  --only GLOB       only textures whose name matches (* and ?)
  --force           regenerate even what the manifest says is up to date
  --dry-run         classify and report; write nothing
  --quiet           print only failures and the summary
  --print-default-rules   print the built-in rules as JSON
```

`--game` takes an install directory (KotOR's `chitin.key` and its BIFs, `texturepacks/`,
`modules/`, `override/`; case-insensitive, so a copied install works on Linux) or a single
archive or texture file. Where one name exists in several places the first wins, in the
engine's order: override, modules, texture packs, key/BIF; a TPC beats a TGA of the same place.

### Progress

One line per texture on stdout, easy for a launcher's progress bar to parse:

```
texgen: 3412 textures found
progress 1/3412 lda_wall01 generated surface
progress 2/3412 fx_flame skipped class effect (fx_*)
progress 3/3412 p_bastilah01 unchanged generated
progress 4/3412 broken_one FAILED <reason>
texgen: done: 3000 generated, 380 unchanged, 31 skipped, 1 failed (of 3412)
```

The exit code is 1 when a texture failed, 2 for bad arguments.

## Output

Everything lands flat in `--out`:

| file | what |
|---|---|
| `<name>_n.dds` | tangent-space normal map, BC5 (x and y; the shader rebuilds z), full mip chain |
| `<name>_m.dds` | packed material map, BC7 (or BC1 with `--material-codec bc1`), full mip chain |
| `texgen_manifest.json` | what was done, per texture |

`--format tga` writes `<name>_n.tga` and `<name>_m.tga` (24-bit, no mip maps) instead. Maps have
the size of the source texture and use the engine's row order (the first row stored is v = 0,
the bottom row of a TGA; this is the reverse of what DDS viewers assume, so a viewer shows the
image upside down). Mip levels are 2x2 box averages; normal-map levels are averaged as vectors
and renormalised. The encoders are the vendored bc7enc_rdo ones (`extern/bcenc`, MIT or public
domain). Switching format regenerates the maps and deletes the other format's files, so a stale
file never shadows a new one; the engine prefers a `.dds` when both exist.

**Size.** For a texture of N pixels a 24-bit TGA map is 3N bytes; a BC5 or BC7 map is N bytes
(8 bits per pixel) and BC1 is N/2, plus a third more for the mip chain. A normal plus a
material map is therefore about 2.2x smaller with BC5+BC7 and 3x smaller with BC5+BC1; without
the mip chains 3x and 4x. BC7 is the default because it keeps roughness gradients without banding.

**Normal map.** Height comes from the texture's luma: a blur separates the low frequencies from the
detail, and the height is the detail plus a quarter of the low frequencies. A 3x3 Sobel filter gives
the slope, scaled by the strength, and the normal is `(-sx, -sy, 1)` normalised and stored as
`n * 0.5 + 0.5` (x right, y up, z out). Edges **wrap** (textures tile). A flat texture gives a flat map
(128, 128, 255).

**Material map.** Red is roughness, green a specular/metalness hint, blue 0.
Roughness starts at 0.8 and rises with local contrast (standard deviation of luma over a small
window) and falls a little with brightness. A texture whose TXI names an environment map
(`envmaptexture`) or a bumpy-shiny map (`bumpyshinytexture`) is "shiny": smoother (x0.6), with a
metalness hint of `0.5 + 0.5 * brightness`.

**Left alone.** A texture is skipped when the game has its own bump map (the TXI's
`bumpmaptexture`, or a game texture named `<name>_n`), when it is a cube map, an animated or
procedural texture, or itself named `*_n` / `*_m` (so running texgen over its own output does
nothing). Skips are recorded in the manifest too.

### The manifest

```json
{"tool": "texgen", "version": 1, "textures": [
  {"texture": "lda_wall01", "class": "surface", "status": "generated", "strength": 2.0, "sourceHash": "9d3c..."},
  {"texture": "fx_flame", "class": "effect", "status": "skipped", "reason": "class effect (fx_*)", "strength": 2.0, "sourceHash": "41ab..."}]}
```

`sourceHash` is a 64-bit FNV-1a of the source file bytes (and a loose TXI) plus the settings that
made the output: the tool version, the base strength and the rules. A run **resumes**: a texture
whose hash is unchanged and whose outputs still exist is left alone (reported `unchanged`); change
the source, the strength or the rules, or delete an output, and it is redone. A texture that stops
getting maps (a rule now skips it) has its old maps removed. The manifest is written every 200
textures and at the end, through a temporary file, so an interrupted run loses little and never
leaves a half-written manifest. A texture that fails is not recorded and is tried again next time.

## Rules

A rules file decides, per texture, whether to make maps and how strongly. The first matching rule wins;
a texture nothing matches is a plain `surface` at the base strength.

```json
{"version": 1, "rules": [
  {"match": "*_lm*", "class": "lightmap", "action": "skip"},
  {"match": "p_*", "class": "face", "scale": 0.35},
  {"match": "special_*", "class": "special", "strength": 5.0},
  {"match": "*", "decal": true, "class": "decal", "action": "skip"},
  {"match": "*", "cutout": true, "class": "foliage", "scale": 0.5}]}
```

* `match`: a case-insensitive glob on the texture name (`*`, `?`).
* `class`: a label for the manifest.
* `action`: `generate` (default) or `skip`.
* `strength`: an absolute strength; or `scale`: a multiplier of the base strength (default 1).
* Conditions (all optional): `cutout`, `decal`, `font`, `shiny` (booleans taken from the texture and
  its TXI), `minSize` / `maxSize` (the larger side in pixels).

The built-in rules (`texgen --print-default-rules`) come from public naming conventions: lightmaps
`*_lm*` are skipped; TXI fonts and decals are skipped; UI names (`lbl_*`, `ui_*`, `gui_*`, `btn_*`,
`i_*` icons, `po_*` portraits, `fnt_*`) are skipped; effects (`fx_*`, `*_fx`, `flare*`, `glow*`) are
skipped; heads (`p_*`, `n_*`) are generated softened to 35 % of the base strength; alpha-cutout
foliage (punch-through blending, or alpha that is nearly all on or off) is softened to 50 %. They are
a starting point: a later pass will refine them against real content.

## Layout

* `src/libs/texgen`, `include/reone/texgen`: the library (image decoding, generators, rules, manifest,
  sources, job), built into the unit tests.
* `src/apps/texgen`: the command-line tool, which links the library.
* `test/texgen/texgen.cpp`: synthetic-image tests (flat, ramp, dome, tiling, rules, manifest, resume,
  archives of every kind the scanner reads).

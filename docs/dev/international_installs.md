# International installs

## Text encoding

`dialog.tlk` stores its language in the header (0 English, 1 French, 2 German, 3 Italian,
4 Spanish, 5 Polish, 128 Korean, 129 Traditional Chinese, 130 Simplified Chinese,
131 Japanese). The engine converts at the edges and keeps UTF-8 internally:

- English, French, German, Italian, Spanish: Windows-1252. Polish: Windows-1250. Unknown ids
  are read as Windows-1252.
- Reading a TLK decodes each string to UTF-8; writing encodes back (characters the language
  cannot hold become `?`), so read and write round-trip byte for byte.
- Korean, Chinese and Japanese strings are multibyte code pages and are passed through
  unconverted for now.

## Fonts

The game's fonts are textures with one glyph per code-page byte. `Font` maps each UTF-8 code
point back to the byte of the active code page (set when the main `dialog.tlk` loads) and
draws that glyph. A character the font has no glyph for falls back to its base letter
(`é` to `e`), a typographic stand-in (curly quote to `'`), or `?`. Invalid UTF-8 is read as
Latin-1 rather than dropped. Line breaking and elision work on whole characters.

## Co-op fingerprint

Players with different K1 languages can play together. The K1 check compares `chitin.key`
and the module files only; a `dialog.tlk` difference is logged as a warning, as for K2.

## Launcher detection

An install is any folder holding `chitin.key` and `swkotor.exe` / `swkotor2.exe`, whatever its
name, so localised Steam and GOG folder names are found (Steam libraries, `GOG Games`, GOG roots).

## Needs a real localised install to confirm

- Glyph layouts of the localised fonts (assumed byte-indexed by code page).
- Whether `chitin.key` and the module files are identical across languages.
- Actual folder names and GOG product ids per language.
- Korean/Chinese/Japanese text, NWScript byte-based string functions and GFF localised strings
  are not converted.

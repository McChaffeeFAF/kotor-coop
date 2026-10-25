# Building

The engine is a CMake project (CMake 3.16 or newer, C++17). Every program lands in
`build/bin` (with a multi-config generator such as Visual Studio, `build/bin` or
`build/bin/Release`, depending on the CMake version and generator):

| program | what it is | CMake option |
|---|---|---|
| `engine` | the game | always built |
| `tests` | the unit tests (GoogleTest) | `BUILD_TESTS` (ON) |
| `texgen` | the Surface Detail map generator ([texgen.md](texgen.md)) | `BUILD_TEXGEN` (ON) |
| `shaderpack` | packs `glsl/` into `shaderpack.erf` next to the engine; runs as part of every build | always built |
| `launcher`, `toolkit` | upstream reone's launcher and resource toolkit; need wxWidgets | `BUILD_LAUNCHER`, `BUILD_TOOLKIT` (ON) |
| `dataminer`, `gff2json`, `unerf`, `ncsrun` | upstream reone's command-line tools | `BUILD_DATAMINER`, `BUILD_GFF2JSON`, `BUILD_UNERF`, `BUILD_NCSRUN` (ON) |

Other options: `ENABLE_MOVIE` (movie playback, ON), `REONE_NETWORK` (co-op networking
through GameNetworkingSockets, ON), `REONE_ASAN` / `REONE_UBSAN` / `REONE_TSAN`
(sanitizers, GCC/Clang), `REONE_BENCH` (simulation benchmarks in `bench/`) and
`REONE_FUZZ` (libFuzzer targets in `fuzz/`, Clang only).

Building needs no game files. Running the engine needs your own KotOR install.

## Windows

Install Visual Studio 2022 or newer with the "Desktop development with C++" workload, and
[vcpkg](https://github.com/microsoft/vcpkg). Then, from a Developer Command Prompt:

```
vcpkg install --triplet x64-windows boost-program-options boost-json boost-format ^
    boost-algorithm boost-tokenizer sdl3 gtest libmad openal-soft ^
    ffmpeg gamenetworkingsockets openssl protobuf
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake ^
    -DBUILD_TOOLKIT=OFF -DBUILD_LAUNCHER=OFF
cmake --build build --config Release --parallel
```

Add `wxwidgets` to the vcpkg list if you want the toolkit or the old launcher.

## Linux

`tools/cloud/linux_deps.sh` installs everything on Ubuntu 24.04: the apt packages, and
SDL3 and GameNetworkingSockets built from source (Ubuntu 24.04 packages neither; they go
to `/opt/sdl3` and `/opt/gns` by default). It exports the paths the build needs, so source
it in the shell that runs CMake:

```
source tools/cloud/linux_deps.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TOOLKIT=OFF -DBUILD_LAUNCHER=OFF
cmake --build build
```

`tools/cloud/linux_test.sh` does all of it and then runs the unit tests (the GitHub
Actions workflow, `.github/workflows/build-test-linux.yml`, runs exactly this on every
push to `main` and every pull request):

```
tools/cloud/linux_test.sh                       # deps, configure, build `tests`, run them
SKIP_DEPS=1 tools/cloud/linux_test.sh           # dependencies already installed
BUILD_DIR=build-linux tools/cloud/linux_test.sh --gtest_filter='Coop*'
```

Other distributions need the same libraries: Boost, SDL3, GoogleTest/GoogleMock, libmad,
OpenAL, FFmpeg and GameNetworkingSockets (with OpenSSL and Protobuf), plus OpenGL
development headers.

On GCC and Clang the build passes `-ffp-contract=off`, so that floating-point results
match MSVC's (see [DETERMINISM.md](DETERMINISM.md)). Keep it if you change the flags.

## Packages for players

`tools/package/make_tester_package.ps1` (Windows) and `tools/package/make_linux_package.sh`
(Linux) build a portable folder with the engine, its libraries and launcher scripts. Both
refuse to build a package that would contain game files or texgen output.

## Running

```
engine --game <KotOR folder> [--k2game <KotOR II folder>]
engine --help                                   # every option
```

The engine writes `engine.log` to its working directory (the package folder when it is started from the launcher scripts).

#!/usr/bin/env bash
# Build the Linux tester package: a tar.gz with the engine, the libraries it
# needs (found next to it through an $ORIGIN rpath), the bash launch scripts and
# NO game files. Made locally only; nothing is uploaded or sent.
#
#   tools/package/make_linux_package.sh [--bin DIR] [--out DIR] [--server HOST:PORT]
#                                       [--release NAME] [--lib-path DIR[:DIR...]] [--with-texgen]
#
# --bin      the build's bin directory (default build-linux/bin or build/bin)
# --lib-path extra directories to search for libraries (SDL3, GameNetworkingSockets
#            when built into a prefix such as /opt/sdl3/lib:/opt/gns/lib)
# --server   the join-code service baked into coop-defaults.cfg
# --release  names a release (KotOR-Coop-Linux-<release>)
# --with-texgen  also ships bin/texgen, the enhanced-texture generator (docs/texgen.md).
#            Off by default: the package is the engine only. texgen's OUTPUT is derived
#            from the player's game files and is never shipped, whatever this flag says.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)"
BIN=""
OUT=""
SERVER=""
RELEASE=""
WITH_TEXGEN=0
LIB_PATH="${LIB_PATH:-/opt/sdl3/lib:/opt/gns/lib}"
while [ $# -gt 0 ]; do
    case "$1" in
        --bin) BIN=$2; shift 2 ;;
        --out) OUT=$2; shift 2 ;;
        --server) SERVER=$2; shift 2 ;;
        --release) RELEASE=$2; shift 2 ;;
        --lib-path) LIB_PATH=$2; shift 2 ;;
        --with-texgen) WITH_TEXGEN=1; shift ;;
        *) echo "unknown option $1" >&2; exit 2 ;;
    esac
done
if [ -z "$BIN" ]; then
    for candidate in "$REPO/build-linux/bin" "$REPO/build/bin"; do
        [ -x "$candidate/engine" ] && BIN=$candidate && break
    done
fi
[ -n "${BIN:-}" ] && [ -d "$BIN" ] && BIN=$(cd "$BIN" && pwd -P)
[ -x "${BIN:-}/engine" ] || { echo "no engine in '${BIN:-}': build the full engine first (tools/cloud/linux_engine.sh)" >&2; exit 1; }
[ -x "$BIN/shaderpack" ] || { echo "no shaderpack in $BIN" >&2; exit 1; }
command -v patchelf >/dev/null || { echo "patchelf is needed (apt install patchelf)" >&2; exit 1; }
OUT=${OUT:-$REPO/build-linux/package}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd -P)
FILES="$REPO/tools/package/linux/files"

version=$(git -C "$REPO" describe --always --dirty 2>/dev/null || date +%Y%m%d)
if [ -n "$RELEASE" ]; then name="KotOR-Coop-Linux-$RELEASE"; else name="kotor-coop-linux-$version"; fi
stage="$OUT/$name"
rm -rf "$stage"
mkdir -p "$stage/bin" "$stage/lib"

# The engine. The shader pack is rebuilt from glsl/ each time, as for the Windows package.
cp "$BIN/engine" "$stage/bin/engine"
if [ "$WITH_TEXGEN" = 1 ]; then
    [ -x "$BIN/texgen" ] || { echo "--with-texgen: no texgen in $BIN (build with -DBUILD_TEXGEN=ON)" >&2; exit 1; }
    cp "$BIN/texgen" "$stage/bin/texgen"
fi
(cd "$stage" && "$BIN/shaderpack" "$REPO/glsl" "$stage") >/dev/null
[ -f "$stage/shaderpack.erf" ] || { echo "shaderpack failed" >&2; exit 1; }

# Libraries: everything the engine links except the C library and the system's
# graphics stack (those must match the machine's driver), which stay out. The
# C++ runtime stays out too: Mesa and friends on the player's machine need
# theirs, and an older bundled copy would break them.
EXCLUDE='^(ld-linux.*|libc|libm|libdl|libpthread|librt|libutil|libresolv|libnsl|libanl|libBrokenLocale|libstdc\+\+|libgcc_s|libGL|libGLX|libGLdispatch|libOpenGL|libEGL|libGLESv2|libgbm|libdrm.*|libvulkan|libX11|libXext|libxcb.*|libwayland.*|libxkbcommon|libasound|libpulse.*|libpipewire.*|libudev|libdbus-1|libsystemd)\.so'
export LD_LIBRARY_PATH="$LIB_PATH${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
bundle() { # bundle FILE: copies each shared library FILE needs into lib/
    local file=$1 line lib path base
    while IFS= read -r line; do
        lib=$(awk '{print $1}' <<<"$line")
        path=$(awk '/=>/ {print $3}' <<<"$line")
        base=${lib##*/}
        [ -n "$path" ] && [ -f "$path" ] || continue
        if [[ "$base" =~ $EXCLUDE ]]; then continue; fi
        if [ ! -f "$stage/lib/$base" ]; then
            cp -L "$path" "$stage/lib/$base"
            chmod u+w "$stage/lib/$base"
            bundle "$stage/lib/$base"
        fi
    done < <(ldd "$file" 2>/dev/null)
}
bundle "$stage/bin/engine"
patchelf --set-rpath '$ORIGIN/../lib' "$stage/bin/engine"
if [ "$WITH_TEXGEN" = 1 ]; then
    bundle "$stage/bin/texgen"
    patchelf --set-rpath '$ORIGIN/../lib' "$stage/bin/texgen"
fi
for lib in "$stage"/lib/*.so*; do
    [ -e "$lib" ] && patchelf --set-rpath '$ORIGIN' "$lib"
done

# Launch scripts, README, defaults and version.
cp -r "$FILES"/. "$stage/"
chmod +x "$stage"/*.sh
{
    echo "# Written by make_linux_package.sh. setup.sh copies coop-server into reone.cfg."
    [ -n "$SERVER" ] && echo "coop-server=$SERVER"
} >"$stage/coop-defaults.cfg"
glibc=$(objdump -T "$stage/bin/engine" "$stage"/lib/*.so* 2>/dev/null | grep -o 'GLIBC_[0-9.]*' | sort -V | tail -n 1)
{
    [ -n "$RELEASE" ] && echo "release $RELEASE"
    echo "build $version"
    echo "made $(date -u +%Y-%m-%dT%H:%M:%S)"
    echo "needs ${glibc:-unknown}"
    echo "engine sha256 $(sha256sum "$stage/bin/engine" | cut -d' ' -f1)"
} >"$stage/VERSION.txt"

# Guard: no game file may be in the package. Game data types by extension, and
# anything named like an install's key files. shaderpack.erf is reone's own
# shader archive, built from glsl/ in this repo.
bad=$(find "$stage" -type f \( -iregex '.*\.\(bif\|key\|tlk\|rim\|mod\|erf\|sav\|tpc\|tga\|dds\|txi\|2da\|mdl\|mdx\|wok\|pwk\|dwk\|lyt\|vis\|git\|are\|ifo\|dlg\|ncs\|nss\|utc\|utd\|utp\|uti\|utm\|ute\|uts\|utt\|utw\|jrl\|fac\|gui\|ssf\|wav\|mp3\|bik\|lip\)' \
    -o -iname chitin.key -o -iname dialog.tlk -o -iname swkotor.exe -o -iname swkotor2.exe \) ! -name shaderpack.erf)
if [ "$WITH_TEXGEN" != 1 ] && [ -n "$(find "$stage" -iname 'texgen*' -o -name '*_n.tga' -o -name '*_m.tga' -o -name '*_n.dds' -o -name '*_m.dds' -o -name 'texgen_manifest.json' | head -n 1)" ]; then
    echo "package refused: texgen or its output is in the package but --with-texgen was not given" >&2
    exit 1
fi
if [ -n "$(find "$stage" -name '*_n.tga' -o -name '*_m.tga' -o -name '*_n.dds' -o -name '*_m.dds' -o -name 'texgen_manifest.json' | head -n 1)" ]; then
    echo "package refused: it would contain texgen output (derived from game files)" >&2
    exit 1
fi
if [ -n "$bad" ]; then
    echo "package refused: it would contain game files:" >&2
    echo "$bad" >&2
    exit 1
fi
if [ -n "$(find "$stage" -type f -size +200M)" ]; then
    echo "unexpectedly large file in the package" >&2
    exit 1
fi

archive="$OUT/$name.tar.gz"
rm -f "$archive"
tar -C "$OUT" --sort=name --owner=0 --group=0 --numeric-owner -czf "$archive" "$name"
echo "Package: $archive ($(du -h "$archive" | cut -f1), sha256 $(sha256sum "$archive" | cut -d' ' -f1))"
echo "Needs: ${glibc:-unknown}"
(cd "$OUT" && tar -tzf "$archive" | sed -n '1,200p')

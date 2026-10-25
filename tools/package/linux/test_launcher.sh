#!/usr/bin/env bash
# Checks the Linux launcher's detection against synthetic Steam trees (empty
# files in a temp directory; no game data). Run: tools/package/linux/test_launcher.sh
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
FILES="$HERE/files"
fails=0
check() { # check DESCRIPTION EXPECTED ACTUAL
    if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: expected '$2', got '$3'"; fails=$((fails + 1)); fi
}

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
stage() { # stage ROOT: a package folder with the scripts, no engine
    mkdir -p "$1"
    cp -r "$FILES/scripts" "$1/"
    cp "$FILES"/*.sh "$1/"
}
fake_game() { # fake_game DIR EXE [CHITIN]
    mkdir -p "$1"
    : >"$1/${3:-chitin.key}"
    : >"$1/$2"
}
detect() { # detect HOME which -> prints the found path or "none"
    (
        export HOME="$1" USER=tester
        . "$FILES/scripts/detect.sh"
        # Steam Deck paths outside HOME would leak the real machine into the test.
        steam_roots() {
            local dir
            for dir in "$HOME/.steam/steam" "$HOME/.local/share/Steam" "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam"; do
                [ -d "$dir/steamapps" ] && (cd "$dir" && pwd -P)
            done | sort -u
        }
        extra=${EXTRA_APPS:-}
        if [ -n "$extra" ]; then
            real_steam_apps=$(declare -f steam_apps)
            steam_apps() { { echo "$extra"; (cd "$HOME" && for r in $(steam_roots); do echo "$r/steamapps"; done); } | sort -u; }
        fi
        hit=$(find_install "$2") && echo "${hit#*$'\t'}" || echo none
    )
}

# 1. Plain Steam install of both games under ~/.local/share/Steam.
h="$work/h1"
mkdir -p "$h/.local/share/Steam/steamapps/common"
fake_game "$h/.local/share/Steam/steamapps/common/swkotor" swkotor.exe
fake_game "$h/.local/share/Steam/steamapps/common/Knights of the Old Republic II" swkotor2.exe
check "k1 in the default Steam folder" "$h/.local/share/Steam/steamapps/common/swkotor" "$(detect "$h" k1)"
check "k2 in the default Steam folder" "$h/.local/share/Steam/steamapps/common/Knights of the Old Republic II" "$(detect "$h" k2)"

# 2. A renamed install folder, found through the app manifest.
h="$work/h2"
mkdir -p "$h/.steam/steam/steamapps/common"
fake_game "$h/.steam/steam/steamapps/common/My KotOR" swkotor.exe
printf '"AppState"\n{\n\t"appid"\t"32370"\n\t"installdir"\t"My KotOR"\n}\n' >"$h/.steam/steam/steamapps/appmanifest_32370.acf"
check "renamed folder via appmanifest" "$(cd "$h/.steam/steam" && pwd -P)/steamapps/common/My KotOR" "$(detect "$h" k1)"

# 3. A second library on another drive, listed in libraryfolders.vdf.
h="$work/h3"
lib="$work/drive2/SteamLibrary"
mkdir -p "$h/.steam/steam/steamapps" "$lib/steamapps/common"
printf '"libraryfolders"\n{\n\t"0"\n\t{\n\t\t"path"\t\t"%s"\n\t}\n\t"1"\n\t{\n\t\t"path"\t\t"%s"\n\t}\n}\n' "$h/.steam/steam" "$lib" >"$h/.steam/steam/steamapps/libraryfolders.vdf"
fake_game "$lib/steamapps/common/Knights of the Old Republic II" swkotor2.exe
check "k2 in another library from libraryfolders.vdf" "$(cd "$lib" && pwd -P)/steamapps/common/Knights of the Old Republic II" "$(detect "$h" k2)"

# 4. File names in other cases (the games shipped on a case-insensitive file system).
h="$work/h4"
mkdir -p "$h/.local/share/Steam/steamapps/common"
fake_game "$h/.local/share/Steam/steamapps/common/swkotor" SWKOTOR.EXE Chitin.KEY
check "chitin.key and exe matched ignoring case" "$h/.local/share/Steam/steamapps/common/swkotor" "$(detect "$h" k1)"

# 5. A folder with the exe but no chitin.key is not an install. What identifies a game is its exe, not
# its folder name: a K1 install in a folder called "...Republic II" is the K1 install, and the
# K2 search finds nothing.
h="$work/h5"
mkdir -p "$h/.local/share/Steam/steamapps/common/swkotor" "$h/.local/share/Steam/steamapps/common/Knights of the Old Republic II"
: >"$h/.local/share/Steam/steamapps/common/swkotor/swkotor.exe"
fake_game "$h/.local/share/Steam/steamapps/common/Knights of the Old Republic II" swkotor.exe
check "no chitin.key: not an install; the exe, not the name, tells the game" "$h/.local/share/Steam/steamapps/common/Knights of the Old Republic II" "$(detect "$h" k1)"
check "k2 folder without swkotor2.exe: not an install" "none" "$(detect "$h" k2)"

# 6. Flatpak Steam.
h="$work/h6"
fp="$h/.var/app/com.valvesoftware.Steam/.local/share/Steam"
mkdir -p "$fp/steamapps/common"
fake_game "$fp/steamapps/common/swkotor" swkotor.exe
check "Flatpak Steam" "$(cd "$fp" && pwd -P)/steamapps/common/swkotor" "$(detect "$h" k1)"

# 7. A Steam Deck SD card library (outside HOME).
h="$work/h7"
sd="$work/run/media/mmcblk0p1"
mkdir -p "$h/.steam/steam/steamapps" "$sd/steamapps/common"
fake_game "$sd/steamapps/common/swkotor" swkotor.exe
check "SD card library" "$(cd "$sd" && pwd -P)/steamapps/common/swkotor" "$(EXTRA_APPS="$sd/steamapps" detect "$h" k1)"

# 7b. Localised folder names (French, Italian, Spanish and Polish stores and installers), and
# a folder of any name: what counts is chitin.key plus the game's exe.
h="$work/h7b"
c="$h/.local/share/Steam/steamapps/common"
mkdir -p "$c"
fake_game "$c/Star Wars Chevaliers de l'Ancienne République" swkotor.exe
fake_game "$c/Star Wars Rycerze Starej Republiki II Lordowie Sithów" swkotor2.exe
check "k1 under its French folder name" "$c/Star Wars Chevaliers de l'Ancienne République" "$(detect "$h" k1)"
check "k2 under its Polish folder name" "$c/Star Wars Rycerze Starej Republiki II Lordowie Sithów" "$(detect "$h" k2)"
h="$work/h7c"
c="$h/.local/share/Steam/steamapps/common"
mkdir -p "$c"
fake_game "$c/Star Wars Caballeros de la Antigua República" swkotor.exe
fake_game "$c/Star Wars Cavalieri della Vecchia Repubblica II I Signori dei Sith" swkotor2.exe
check "k1 under its Spanish folder name" "$c/Star Wars Caballeros de la Antigua República" "$(detect "$h" k1)"
check "k2 under its Italian folder name" "$c/Star Wars Cavalieri della Vecchia Repubblica II I Signori dei Sith" "$(detect "$h" k2)"
h="$work/h7d"
c="$h/.local/share/Steam/steamapps/common"
mkdir -p "$c"
fake_game "$c/Eine ganz andere Bezeichnung" swkotor.exe
fake_game "$c/Another Game" other.exe
check "k1 in a folder of any name" "$c/Eine ganz andere Bezeichnung" "$(detect "$h" k1)"
check "no k2 among other games" "none" "$(detect "$h" k2)"
# GOG's folders: the German edition keeps the English title, the offline installer's folder is free.
h="$work/h7e"
mkdir -p "$h/GOG Games"
fake_game "$h/GOG Games/STAR WARS Knights of the Old Republic II - Die Sith Lords" swkotor2.exe
fake_game "$h/GOG Games/Kotor Eins" swkotor.exe
check "k2 in GOG Games under its German title" "$h/GOG Games/STAR WARS Knights of the Old Republic II - Die Sith Lords" "$(detect "$h" k2)"
check "k1 in GOG Games under a free name" "$h/GOG Games/Kotor Eins" "$(detect "$h" k1)"

# 7f. Lutris and Heroic Wine prefixes (GOG installs inside drive_c), and Flatpak Heroic.
h="$work/h7f"
fake_game "$h/Games/kotor/drive_c/GOG Games/Star Wars - KotOR" swkotor.exe
fake_game "$h/Games/Heroic/Prefixes/kotor2/pfx/drive_c/Program Files (x86)/LucasArts/SWKotOR2" swkotor2.exe
check "k1 in a Lutris prefix" "$h/Games/kotor/drive_c/GOG Games/Star Wars - KotOR" "$(detect "$h" k1)"
check "k2 in a Heroic (Proton-style) prefix" "$h/Games/Heroic/Prefixes/kotor2/pfx/drive_c/Program Files (x86)/LucasArts/SWKotOR2" "$(detect "$h" k2)"
h="$work/h7g"
fake_game "$h/.var/app/com.heroicgameslauncher.hgl/Games/Heroic/Star Wars KotOR" swkotor.exe
check "k1 in the Flatpak Heroic folder" "$h/.var/app/com.heroicgameslauncher.hgl/Games/Heroic/Star Wars KotOR" "$(detect "$h" k1)"

# 7h. Mixed-case Steam folders (SteamApps/Common).
h="$work/h7h"
fake_game "$h/.local/share/Steam/steamapps/Common/SWKOTOR" SWKOTOR.EXE CHITIN.KEY
check "mixed-case Common folder" "$h/.local/share/Steam/steamapps/Common/SWKOTOR" "$(detect "$h" k1)"

# 8. Setup writes reone.cfg non-interactively from a pre-seeded answer set.
h="$work/h8"
root="$work/pkg"
stage "$root"
mkdir -p "$h/.local/share/Steam/steamapps/common"
fake_game "$h/.local/share/Steam/steamapps/common/swkotor" swkotor.exe
fake_game "$h/.local/share/Steam/steamapps/common/Knights of the Old Republic II" swkotor2.exe
printf 'coop-server=join.example.org:27200\n' >"$root/coop-defaults.cfg"
printf 'y\ny\nAlice\nk2\n' | HOME="$h" USER=tester "$root/setup.sh" >/dev/null 2>&1
check "setup: game" "$h/.local/share/Steam/steamapps/common/swkotor" "$(. "$FILES/scripts/detect.sh"; cfg_get "$root/reone.cfg" game)"
check "setup: k2game" "$h/.local/share/Steam/steamapps/common/Knights of the Old Republic II" "$(. "$FILES/scripts/detect.sh"; cfg_get "$root/reone.cfg" k2game)"
check "setup: name" "Alice" "$(. "$FILES/scripts/detect.sh"; cfg_get "$root/reone.cfg" coop-name)"
check "setup: service address from the package defaults" "join.example.org:27200" "$(. "$FILES/scripts/detect.sh"; cfg_get "$root/reone.cfg" coop-server)"

# 8b. KotOR II is optional: with only KotOR 1 the setup skips it, drops k2game
# and picks the KotOR look (hud k1), so a K1-only owner can still host and join.
h="$work/h8b"
root="$work/pkg-k1only"
stage "$root"
mkdir -p "$h/.local/share/Steam/steamapps/common"
fake_game "$h/.local/share/Steam/steamapps/common/swkotor" swkotor.exe
printf 'y\n\nBob\n\n' | HOME="$h" USER=tester "$root/setup.sh" >/dev/null 2>&1
check "setup k1-only: game" "$h/.local/share/Steam/steamapps/common/swkotor" "$(. "$FILES/scripts/detect.sh"; cfg_get "$root/reone.cfg" game)"
check "setup k1-only: no k2game" "" "$(. "$FILES/scripts/detect.sh"; cfg_get "$root/reone.cfg" k2game)"
check "setup k1-only: hud k1" "k1" "$(. "$FILES/scripts/detect.sh"; cfg_get "$root/reone.cfg" hud)"
check "setup k1-only: name" "Bob" "$(. "$FILES/scripts/detect.sh"; cfg_get "$root/reone.cfg" coop-name)"

# 9. Host and join pass the right engine arguments (a stub engine records them).
# Back to test 8's package: it has the join-code service address.
h="$work/h8"
root="$work/pkg"
mkdir -p "$root/bin"
printf '#!/bin/sh\nprintf "%%s\\n" "$@" >"$PWD/args.txt"\n' >"$root/bin/engine"
chmod +x "$root/bin/engine"
HOME="$h" "$root/host.sh" >/dev/null 2>&1
check "host: --host first" "--host" "$(sed -n 1p "$root/args.txt")"
check "host: records the session" "--coop-record" "$(sed -n 2p "$root/args.txt")"
HOME="$h" "$root/join.sh" "k7qm-4txr!" >/dev/null 2>&1
check "join: code cleaned and passed" "k7qm-4txr" "$(sed -n 2p "$root/args.txt")"
HOME="$h" "$root/join.sh" "203.0.113.7" >/dev/null 2>&1
check "join with a server: a bare address is passed as typed" "203.0.113.7" "$(sed -n 2p "$root/args.txt")"
# Without a join-code server (a public build): join by IP:port.
grep -v '^coop-server=' "$root/reone.cfg" >"$root/reone.cfg.tmp" && mv "$root/reone.cfg.tmp" "$root/reone.cfg"
HOME="$h" "$root/join.sh" "203.0.113.7" >/dev/null 2>&1
check "join without a server: a bare IP gets the default port" "203.0.113.7:27015" "$(sed -n 2p "$root/args.txt")"
HOME="$h" "$root/join.sh" "203.0.113.7:28000" >/dev/null 2>&1
check "join without a server: ip:port is passed as typed" "203.0.113.7:28000" "$(sed -n 2p "$root/args.txt")"
HOME="$h" "$root/play.sh" solo >/dev/null 2>&1
check "solo: no --host or --join" "--coop-record" "$(sed -n 1p "$root/args.txt")"

# 10. On a Steam Deck setup leaves the window settings to the engine's Deck defaults.
h="$work/h10"
root="$work/pkg10"
stage "$root"
mkdir -p "$h/.local/share/Steam/steamapps/common"
fake_game "$h/.local/share/Steam/steamapps/common/swkotor" swkotor.exe
fake_game "$h/.local/share/Steam/steamapps/common/Knights of the Old Republic II" swkotor2.exe
printf 'y\ny\nDeckUser\nk1\n' | HOME="$h" USER=tester SteamDeck=1 "$root/setup.sh" >/dev/null 2>&1
check "deck: no window override written" "" "$(. "$FILES/scripts/detect.sh"; cfg_get "$root/reone.cfg" fullscreen)"
check "deck: game still written" "$h/.local/share/Steam/steamapps/common/swkotor" "$(. "$FILES/scripts/detect.sh"; cfg_get "$root/reone.cfg" game)"

[ "$fails" -eq 0 ] && echo "all launcher checks passed" || { echo "$fails launcher check(s) failed"; exit 1; }

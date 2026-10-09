# Finds the KotOR and KotOR II installs. It only reads the Steam folders and the
# game folders; it never writes to them. Sourced by setup.sh and play.sh.
#
# An install is a folder with chitin.key and the game's exe (swkotor.exe or
# swkotor2.exe). Names are matched ignoring case, as the games shipped on a
# case-insensitive file system.

K1_APP=32370
K2_APP=208580
K1_EXE=swkotor.exe
K2_EXE=swkotor2.exe
K1_TITLE="Knights of the Old Republic"
K2_TITLE="Knights of the Old Republic II"
K1_NAMES=("swkotor" "Star Wars Knights of the Old Republic" "Star Wars - Knights of the Old Republic"
    # Folder names the localised stores and installers give (French, German, Italian, Spanish, Polish),
    # with and without the accents some file systems drop.
    "Star Wars Chevaliers de l'Ancienne République" "Star Wars - Chevaliers de l'Ancienne République" "Star Wars Chevaliers de l'Ancienne Republique"
    "Star Wars Cavalieri della Vecchia Repubblica" "Star Wars - Cavalieri della Vecchia Repubblica"
    "Star Wars Caballeros de la Antigua República" "Star Wars - Caballeros de la Antigua República" "Star Wars Caballeros de la Antigua Republica"
    "Star Wars Rycerze Starej Republiki" "Star Wars - Rycerze Starej Republiki"
    "STAR WARS Knights of the Old Republic" "Star Wars Knights of the Old Republic - Deutsch")
K2_NAMES=("Knights of the Old Republic II" "swkotor2" "Star Wars Knights of the Old Republic II"
    "Star Wars Knights of the Old Republic II - The Sith Lords" "Star Wars - Knights of the Old Republic II - The Sith Lords"
    "Star Wars Knights of the Old Republic II Die Sith Lords" "Star Wars - Knights of the Old Republic II - Die Sith Lords"
    "Star Wars Chevaliers de l'Ancienne République II Les Seigneurs Sith" "Star Wars - Chevaliers de l'Ancienne République II - Les Seigneurs Sith"
    "Star Wars Cavalieri della Vecchia Repubblica II I Signori dei Sith" "Star Wars - Cavalieri della Vecchia Repubblica II - I Signori dei Sith"
    "Star Wars Caballeros de la Antigua República II Los Señores Sith" "Star Wars - Caballeros de la Antigua República II - Los Señores Sith"
    "Star Wars Caballeros de la Antigua Republica II Los Senores Sith"
    "Star Wars Rycerze Starej Republiki II Lordowie Sithów" "Star Wars - Rycerze Starej Republiki II - Lordowie Sithów")

# find_ci DIR NAME: the first entry of DIR whose name equals NAME ignoring case.
find_ci() {
    [ -d "$1" ] || return 1
    find "$1" -maxdepth 1 -iname "$2" -print -quit 2>/dev/null
}

# test_install DIR EXE: DIR has chitin.key and EXE.
test_install() {
    [ -n "$1" ] && [ -d "$1" ] || return 1
    [ -n "$(find_ci "$1" chitin.key)" ] && [ -n "$(find_ci "$1" "$2")" ]
}

# children DIR: the folders directly inside DIR. A localised install can sit in a folder of any
# name; the check that matters (chitin.key plus the game's exe) does not depend on the name.
children() {
    [ -d "$1" ] || return 0
    find "$1" -mindepth 1 -maxdepth 1 -type d -print 2>/dev/null | sort
}

# Folders where GOG installs (the offline installers under Wine, Heroic, Lutris) usually put games.
gog_roots() {
    local dir
    for dir in "$HOME/GOG Games" "$HOME/Games/GOG" "$HOME/Games/gog" "$HOME/Games/Heroic" "$HOME/Games" \
        "$HOME/.wine/drive_c/GOG Games" "$HOME/.wine/drive_c/Program Files (x86)/GOG Galaxy/Games"; do
        [ -d "$dir" ] && printf '%s\n' "$dir"
    done
}

# Every Steam root that exists: the usual folders on a desktop, the Flatpak Steam,
# and the Steam Deck (the user is "deck"; the SD card mounts under /run/media).
steam_roots() {
    local candidates=(
        "$HOME/.steam/steam"
        "$HOME/.steam/root"
        "$HOME/.local/share/Steam"
        "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam"
        "$HOME/.var/app/com.valvesoftware.Steam/.steam/steam"
        "/home/deck/.steam/steam"
        "/home/deck/.local/share/Steam"
        "/usr/share/steam"
    )
    local dir
    for dir in "${candidates[@]}"; do
        [ -d "$dir/steamapps" ] && (cd "$dir" && pwd -P)
    done | sort -u
}

# Every Steam library's steamapps folder: each root's own, the ones
# libraryfolders.vdf lists (other drives), and an SD card library on a Steam Deck.
steam_apps() {
    local root vdf path dir
    {
        while IFS= read -r root; do
            echo "$root/steamapps"
            vdf="$root/steamapps/libraryfolders.vdf"
            if [ -f "$vdf" ]; then
                grep -o '"path"[[:space:]]*"[^"]*"' "$vdf" 2>/dev/null |
                    sed -E 's/^"path"[[:space:]]*"([^"]*)"$/\1/; s/\\\\/\\/g' |
                    while IFS= read -r path; do echo "$path/steamapps"; done
            fi
        done < <(steam_roots)
        for dir in /run/media/mmcblk0p1 /run/media/deck/* /run/media/"${USER:-}"/* /media/"${USER:-}"/* /mnt/*; do
            echo "$dir/steamapps"
        done
    } | while IFS= read -r dir; do
        [ -d "$dir" ] && (cd "$dir" && pwd -P)
    done | sort -u
}

# candidates K1|K2: folders that might hold the game, most reliable first, as
# "Source<TAB>Path" lines.
candidates() {
    local which=$1 app names apps manifest installdir name found
    if [ "$which" = k1 ]; then app=$K1_APP; names=("${K1_NAMES[@]}"); else app=$K2_APP; names=("${K2_NAMES[@]}"); fi
    while IFS= read -r apps; do
        # The app manifest names the real install folder, even a renamed one.
        manifest=$(find_ci "$apps" "appmanifest_${app}.acf")
        if [ -n "$manifest" ]; then
            installdir=$(grep -o '"installdir"[[:space:]]*"[^"]*"' "$manifest" 2>/dev/null |
                sed -E 's/^"installdir"[[:space:]]*"([^"]*)"$/\1/' | head -n 1)
            if [ -n "$installdir" ]; then
                found=$(find_ci "$apps/common" "$installdir")
                [ -n "$found" ] && printf 'Steam\t%s\n' "$found"
            fi
        fi
        for name in "${names[@]}"; do
            found=$(find_ci "$apps/common" "$name")
            [ -n "$found" ] && printf 'Steam\t%s\n' "$found"
        done
        # Any other folder of the library: the games' localised store names are not all listed.
        while IFS= read -r found; do
            printf 'Steam\t%s\n' "$found"
        done < <(children "$apps/common")
    done < <(steam_apps)
    local root
    while IFS= read -r root; do
        for name in "${names[@]}"; do
            found=$(find_ci "$root" "$name")
            [ -n "$found" ] && printf 'GOG\t%s\n' "$found"
        done
        while IFS= read -r found; do
            printf 'GOG\t%s\n' "$found"
        done < <(children "$root")
    done < <(gog_roots)
}

# find_install k1|k2: prints "Source<TAB>Path" for the first real install, or nothing.
find_install() {
    local which=$1 exe source path
    if [ "$which" = k1 ]; then exe=$K1_EXE; else exe=$K2_EXE; fi
    while IFS=$'\t' read -r source path; do
        if test_install "$path" "$exe"; then
            printf '%s\t%s\n' "$source" "$path"
            return 0
        fi
    done < <(candidates "$which")
    return 1
}

# reone.cfg is key=value lines. cfg_get FILE KEY; cfg_set FILE KEY VALUE.
cfg_get() {
    [ -f "$1" ] || return 0
    sed -nE "s/^[[:space:]]*$2[[:space:]]*=[[:space:]]*(.*[^[:space:]])?[[:space:]]*$/\1/p" "$1" | head -n 1
}

cfg_set() {
    local file=$1 key=$2 value=$3 tmp
    tmp=$(mktemp)
    if [ -f "$file" ]; then
        grep -vE "^[[:space:]]*$key[[:space:]]*=" "$file" >"$tmp" || true
    fi
    printf '%s=%s\n' "$key" "$value" >>"$tmp"
    mv "$tmp" "$file"
}

# The co-op service address the packager baked into this build.
default_server() {
    cfg_get "$1/coop-defaults.cfg" coop-server
}

clean_name() {
    local name
    name=$(printf '%s' "$1" | tr -cd '[:alnum:] ._-' | sed -E 's/^ +| +$//g')
    printf '%s' "${name:0:24}"
}

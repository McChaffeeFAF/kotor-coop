#!/usr/bin/env bash
# First-run setup: find the KotOR and KotOR II installs and write reone.cfg.
# It only reads the game folders; it never writes to them.
set -u
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)/scripts/common.sh"

ask_folder() {
    local which=$1 title exe hit source path answer
    if [ "$which" = k1 ]; then title=$K1_TITLE; exe=$K1_EXE; else title=$K2_TITLE; exe=$K2_EXE; fi
    if hit=$(find_install "$which"); then
        source=${hit%%$'\t'*}
        path=${hit#*$'\t'}
        say "$title found ($source): $path" >&2
        if yes_no "Use it?"; then
            printf '%s' "$path"
            return
        fi
    fi
    while true; do
        answer=$(ask "Path of your $title folder (the one with $exe):" "")
        answer=${answer//\"/}
        answer=${answer/#\~/$HOME}
        if test_install "$answer" "$exe"; then
            (cd "$answer" && pwd -P) | tr -d '\n'
            return
        fi
        say "That folder has no chitin.key and $exe. Try again." >&2
        # Without a terminal there is nobody to re-ask in a loop.
        [ -t 0 ] || [ -n "$(command -v zenity)$(command -v kdialog)" ] || exit 1
    done
}

say "KotOR co-op test build: setup"
k1=$(ask_folder k1)
k2=$(ask_folder k2)
cfg_set "$CONFIG" game "$k1"
cfg_set "$CONFIG" k2game "$k2"

name=""
while [ -z "$name" ]; do
    name=$(clean_name "$(ask "Your name in co-op (what other players see):" "${USER:-Player}")")
done
cfg_set "$CONFIG" coop-name "$name"

hud=$(ask "HUD style: k1 (KotOR look) or k2 (KotOR II look)" "k1")
[ "$hud" = k2 ] && cfg_set "$CONFIG" hud k2 || cfg_set "$CONFIG" hud k1

server=$(default_server "$ROOT")
[ -n "$server" ] && cfg_set "$CONFIG" coop-server "$server"
if is_steam_deck; then
    # The engine defaults to 1280x800 fullscreen and a larger GUI on a Deck; write nothing that
    # would override that. To change the size of the interface: guiscale=1.5 in reone.cfg.
    say "Steam Deck detected: 1280x800 fullscreen with a larger interface (set guiscale= in reone.cfg to change)."
elif [ -z "$(cfg_get "$CONFIG" borderless)$(cfg_get "$CONFIG" fullscreen)" ]; then
    cfg_set "$CONFIG" borderless 1
    cfg_set "$CONFIG" fullscreen 0
fi
say "Saved $CONFIG"
if [ -z "$(cfg_get "$CONFIG" coop-server)" ]; then
    say "Note: this build has no co-op service address, so join codes will not work (ip:port joins still do)."
fi

#!/usr/bin/env bash
# One entry point: finds your games the first time, then Host / Join / Play solo.
#   ./play.sh            a small menu
#   ./play.sh host       host a co-op game
#   ./play.sh join CODE  join with a code or ip:port
#   ./play.sh solo       play without co-op
#   ./play.sh setup      choose the game folders again
#   ./play.sh report     pack logs and settings for a bug report
# On a Steam Deck, add this file as a non-Steam game in Desktop Mode; with no
# terminal the questions appear as dialogs.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
. "$ROOT/scripts/common.sh"

mode=${1:-}
if [ -z "$mode" ]; then
    ensure_setup
    name=$(cfg_get "$CONFIG" coop-name)
    say "KotOR co-op — playing as ${name:-Player}"
    say "  1) Host a co-op game"
    say "  2) Join a co-op game"
    say "  3) Play solo"
    say "  4) Change game folders / name"
    say "  5) Report a bug"
    choice=$(ask "Choose 1-5:" "")
    case "$choice" in
        1) mode=host ;;
        2) mode=join ;;
        3) mode=solo ;;
        4) mode=setup ;;
        5) mode=report ;;
        *) say "Nothing chosen."; exit 0 ;;
    esac
fi
case "$mode" in
    host) exec "$ROOT/host.sh" ;;
    join) shift; exec "$ROOT/join.sh" "$@" ;;
    solo) ensure_setup; start_engine --coop-record "$(record_path solo)" ;;
    setup) exec "$ROOT/setup.sh" ;;
    report) exec "$ROOT/report-bug.sh" ;;
    *) say "Unknown option '$mode'. Use host, join CODE, solo, setup or report."; exit 2 ;;
esac

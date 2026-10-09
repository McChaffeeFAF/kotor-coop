# Shared by the launch scripts. Everything runs from the package folder.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
CONFIG="$ROOT/reone.cfg"
ENGINE="$ROOT/bin/engine"
LOGS="$ROOT/logs"

# shellcheck source=detect.sh
. "$ROOT/scripts/detect.sh"

# say TEXT: a line for the player, on the terminal or in a dialog when there is none.
say() {
    if [ -t 1 ]; then
        printf '%s\n' "$1"
    elif command -v zenity >/dev/null 2>&1; then
        zenity --info --no-markup --text="$1" 2>/dev/null || true
    elif command -v kdialog >/dev/null 2>&1; then
        kdialog --msgbox "$1" 2>/dev/null || true
    else
        printf '%s\n' "$1" >&2
    fi
}

# ask PROMPT [DEFAULT]: reads an answer from the terminal, or from a dialog when
# there is no terminal (Steam Deck game mode, a file manager).
ask() {
    local prompt=$1 default=${2:-} answer
    if [ -t 0 ]; then
        read -r -p "$prompt${default:+ [$default]} " answer || answer=""
    elif command -v zenity >/dev/null 2>&1; then
        answer=$(zenity --entry --text="$prompt" --entry-text="$default" 2>/dev/null) || answer=""
    elif command -v kdialog >/dev/null 2>&1; then
        answer=$(kdialog --inputbox "$prompt" "$default" 2>/dev/null) || answer=""
    else
        # No terminal and no dialog tool: take the answer from whatever feeds stdin.
        read -r answer || answer=""
    fi
    printf '%s' "${answer:-$default}"
}

# yes_no PROMPT: succeeds for yes (the default).
yes_no() {
    local answer
    answer=$(ask "$1 [Y/n]" "")
    case "$answer" in "" | [Yy]*) return 0 ;; *) return 1 ;; esac
}

need_engine() {
    if [ ! -x "$ENGINE" ]; then
        say "The engine is missing: expected $ENGINE"
        exit 1
    fi
}

ensure_setup() {
    if [ ! -f "$CONFIG" ]; then
        say "First run: setting up the game folders."
        "$ROOT/setup.sh"
    fi
    if [ ! -f "$CONFIG" ]; then
        say "Setup did not finish; run ./setup.sh."
        exit 1
    fi
}

# record_path ROLE: where this run writes its co-op recording (the last 20 are kept).
record_path() {
    mkdir -p "$LOGS"
    # shellcheck disable=SC2012
    ls -1t "$LOGS"/*.jsonl 2>/dev/null | tail -n +21 | while IFS= read -r old; do rm -f "$old"; done
    printf 'logs/%s_%s.jsonl' "$1" "$(date +%Y%m%d_%H%M%S)"
}

# start_engine ARGS...: runs the engine from the package folder, with the bundled
# libraries found next to it.
start_engine() {
    need_engine
    cd "$ROOT"
    export LD_LIBRARY_PATH="$ROOT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    exec "$ENGINE" "$@"
}

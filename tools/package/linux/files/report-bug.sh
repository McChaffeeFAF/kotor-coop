#!/usr/bin/env bash
# Bug report: tar up the game's log, the co-op recordings and the settings.
# No game files and no saves go in. The archive stays here until you send it.
set -u
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)/scripts/common.sh"

stamp=$(date +%Y%m%d_%H%M%S)
work=$(mktemp -d)
mkdir -p "$work/report"
what=$(ask "What happened? (one line, optional)" "")
printf '%s\n' "$what" >"$work/report/description.txt"
for name in engine.log reone.cfg reone_settings.ini; do
    [ -f "$ROOT/$name" ] && cp "$ROOT/$name" "$work/report/"
done
if [ -d "$LOGS" ]; then
    mkdir -p "$work/report/logs"
    # shellcheck disable=SC2012
    ls -1t "$LOGS"/*.jsonl 2>/dev/null | head -n 6 | while IFS= read -r log; do cp "$log" "$work/report/logs/"; done
fi
{
    echo "report: $stamp"
    [ -f "$ENGINE" ] && echo "engine sha256: $(sha256sum "$ENGINE" | cut -d' ' -f1)"
    echo "kernel: $(uname -sr)"
    [ -f /etc/os-release ] && grep -E '^(PRETTY_NAME|VERSION_ID)=' /etc/os-release
    command -v lspci >/dev/null 2>&1 && lspci 2>/dev/null | grep -iE 'vga|3d|display' | sed 's/^/gpu: /'
    command -v glxinfo >/dev/null 2>&1 && glxinfo -B 2>/dev/null | grep -iE 'renderer|OpenGL version' | sed 's/^/gl: /'
    echo "session: ${XDG_SESSION_TYPE:-unknown}"
} >"$work/report/system.txt"
mkdir -p "$ROOT/reports"
archive="$ROOT/reports/bugreport_$stamp.tar.gz"
tar -C "$work" -czf "$archive" report
rm -rf "$work"
say "Made $archive - send it to the developer."

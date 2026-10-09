#!/usr/bin/env bash
# Join a co-op game with the host's code (or ip:port):  ./join.sh [CODE]
set -u
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)/scripts/common.sh"
ensure_setup
code=${1:-}
server=$(cfg_get "$CONFIG" coop-server)
if [ -n "$server" ]; then
    [ -n "$code" ] || code=$(ask "Join code from your host (e.g. K7QM-4TXR) or ip:port:" "")
else
    [ -n "$code" ] || code=$(ask "Your host's IP:port (e.g. 203.0.113.7:27015):" "")
fi
code=$(printf '%s' "$code" | tr -cd 'A-Za-z0-9.:-')
# No join-code service: a bare IP or host name gets the default port.
if [ -z "$server" ] && [ -n "$code" ] && [ "${code#*:}" = "$code" ] && [ "${code#*.}" != "$code" ]; then
    code="$code:27015"
fi
if [ -n "$code" ]; then
    say "Joining $code ..."
    start_engine --join "$code" --coop-record "$(record_path join)"
else
    say "No code: starting the game; use Co-op > Join a game on the main menu."
    start_engine --coop-record "$(record_path join)"
fi

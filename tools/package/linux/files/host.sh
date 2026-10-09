#!/usr/bin/env bash
# Host a co-op game: the game starts hosting; then New Game or Load Game.
set -u
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)/scripts/common.sh"
ensure_setup
if [ -n "$(cfg_get "$CONFIG" coop-server)" ]; then
    say "Starting the game as host. Your join code shows in the Co-op screen and in game under Options > Co-op."
else
    say "Starting the game as host on UDP port 27015. Forward UDP 27015 on your router, then share your public IP and port (like 203.0.113.7:27015)."
fi
start_engine --host --coop-record "$(record_path host)"

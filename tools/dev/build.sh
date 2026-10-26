#!/usr/bin/env bash
# Git Bash wrapper for build.ps1; all arguments are passed through
# (e.g. tools/dev/build.sh -Target tests,engine -Jobs 6).
exec powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$(dirname "$0")/build.ps1")" "$@"

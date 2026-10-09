<#
Run the co-op online service on this Windows machine for tests: signalling
plus the built-in STUN/TURN relay, in one python.exe (no Docker needed).

    powershell -ExecutionPolicy Bypass -File run_local.ps1 [-Lan] [-SignalPort 27200] [-TurnPort 27210]

By default everything listens on 127.0.0.1 only (same-machine tests; the
relay path cannot be exercised there, because ICE never relays to loopback).
-Lan puts STUN/TURN on this machine's LAN address so a relay-forced test
works; signalling stays on 127.0.0.1 unless -LanSignal is also given.
#>
param(
    [switch]$Lan,
    [switch]$LanSignal,
    [int]$SignalPort = 27200,
    [int]$TurnPort = 27210,
    [string]$RelayPorts = "27211-27240"
)
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$ip = "127.0.0.1"
if ($Lan -or $LanSignal) {
    $ip = (Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.IPAddress -notmatch '^(127\.|169\.254\.)' -and $_.PrefixOrigin -ne 'WellKnown' } | Select-Object -First 1).IPAddress
    if (-not $ip) { throw "no LAN IPv4 address found" }
}
$signalHost = if ($LanSignal) { $ip } else { "127.0.0.1" }
$py = (Get-Command python -ErrorAction Stop).Source
& $py (Join-Path $here "coopserver.py") --listen "$($signalHost):$SignalPort" --builtin-turn "$($ip):$TurnPort" `
    --relay-ports $RelayPorts --allow-private-peers --local-stats

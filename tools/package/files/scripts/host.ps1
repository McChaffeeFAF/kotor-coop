# Host a co-op game: the game starts hosting; choose New Game or Load Game.
. (Join-Path $PSScriptRoot "common.ps1")
Test-Setup
$record = Get-RecordPath "host"
. (Join-Path $PSScriptRoot "detect.ps1")
if ((Read-Config $Config)["coop-server"]) {
    Write-Host "Starting the game as host. Your join code shows in the Co-op screen and in game under Options > Co-op."
} else {
    Write-Host "Starting the game as host on UDP port $DefaultCoopPort."
    Write-Host (Get-PortForwardHint $DefaultCoopPort)
}
Start-Engine @("--host", "--coop-record", $record)

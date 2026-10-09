# Join a co-op game with the host's code (or ip:port).
param([string]$Code)
. (Join-Path $PSScriptRoot "common.ps1")
Test-Setup
. (Join-Path $PSScriptRoot "detect.ps1")
$useCodes = [bool](Read-Config $Config)["coop-server"]
if (-not $Code) {
    $Code = $(if ($useCodes) { Read-Host "Join code from your host (e.g. K7QM-4TXR)" } else { Read-Host "Your host's IP:port (e.g. 203.0.113.7:$DefaultCoopPort)" })
}
$Code = ($Code -replace '[^A-Za-z0-9\-\.:]', '').Trim()
if ($Code -and -not $useCodes) {
    $address = Resolve-JoinAddress $Code $DefaultCoopPort
    if (-not $address) { Write-Host "That is not an IP:port (like 203.0.113.7:$DefaultCoopPort)." -ForegroundColor Yellow; exit 1 }
    $Code = $address
}
$record = Get-RecordPath "join"
if ($Code) {
    Write-Host "Joining $Code ..."
    Start-Engine @("--join", $Code, "--coop-record", $record)
} else {
    Write-Host "Nothing to join: starting the game; use Co-op on the main menu."
    Start-Engine @("--coop-record", $record)
}

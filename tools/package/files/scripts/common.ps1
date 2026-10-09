# Shared by the launch scripts. Everything runs from the package folder.
$Root = Split-Path -Parent $PSScriptRoot
$Config = Join-Path $Root "reone.cfg"
$Engine = Join-Path $Root "engine.exe"
$Logs = Join-Path $Root "logs"
# The UDP port a host listens on (engine.exe --host without a number).
$DefaultCoopPort = 27015

# Without a join-code service: what a host forwards and shares.
function Get-PortForwardHint([int]$Port) {
    return "Host: forward UDP $Port on your router, then share your public IP and port (like 203.0.113.7:$Port). Join: type the host's IP:port."
}

# Without a join-code service: the address to join for what was typed. A bare
# IP or host name gets the default port; anything that is not an address
# (a join code, say) is refused with an empty result.
function Resolve-JoinAddress([string]$Text, [int]$Port) {
    $Text = ($Text -replace '[^A-Za-z0-9\-\.:]', '').Trim()
    if ($Text -match '^[A-Za-z0-9\-\.]+:\d{1,5}$') { return $Text }
    if ($Text -match '^[A-Za-z0-9\-]+(\.[A-Za-z0-9\-]+)+$') { return "${Text}:$Port" }
    return ""
}

function Test-Setup {
    if (-not (Test-Path $Config)) {
        Write-Host "First run: setting up the game folders."
        & (Join-Path $PSScriptRoot "setup.ps1")
    }
    if (-not (Test-Path $Config)) {
        throw "Setup did not finish; run Setup.cmd."
    }
}

function Get-RecordPath([string]$Role) {
    New-Item -ItemType Directory -Force $Logs | Out-Null
    # Keep the last 20 recordings.
    Get-ChildItem $Logs -Filter "*.jsonl" | Sort-Object LastWriteTime -Descending | Select-Object -Skip 20 | Remove-Item -Force
    return Join-Path "logs" ("{0}_{1}.jsonl" -f $Role, (Get-Date -Format "yyyyMMdd_HHmmss"))
}

function Start-Engine([string[]]$EngineArgs) {
    Start-Process -FilePath $Engine -ArgumentList $EngineArgs -WorkingDirectory $Root
}

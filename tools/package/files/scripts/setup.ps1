# First-run setup: find the KotOR and KotOR II installs and write reone.cfg.
# It only reads the game folders; it never writes to them.
# (Play KotOR Co-op.cmd does this by itself; this is the console fallback.)
param([switch]$NoDialog)
$Root = Split-Path -Parent $PSScriptRoot
$Config = Join-Path $Root "reone.cfg"
. (Join-Path $PSScriptRoot "detect.ps1")

function Ask-Folder([string]$Which, [bool]$Optional = $false) {
    $game = $Games[$Which]
    $hit = Find-Install $Which
    if ($hit) {
        Write-Host "$($game.Title) found ($($hit.Source)): $($hit.Path)"
        $yes = Read-Host "Use it? [Y/n]"
        if ($yes -eq "" -or $yes -match '^[Yy]') { return $hit.Path }
    }
    while ($true) {
        $answer = $null
        if (-not $NoDialog) {
            try {
                Add-Type -AssemblyName System.Windows.Forms
                $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
                $dialog.Description = "Select your $($game.Title) install folder (the one with $($game.Exe))"
                if ($dialog.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) { $answer = $dialog.SelectedPath }
            } catch { $answer = $null }
        }
        if (-not $answer) { $answer = Read-Host "Path of your $($game.Title) folder (with $($game.Exe))$(if ($Optional) { ', or press Enter to skip' })" }
        $answer = $answer.Trim('"', ' ')
        if (-not $answer -and $Optional) { return "" }
        if (Test-Install $answer $game.Exe) { return $answer }
        Write-Host "That folder has no chitin.key and $($game.Exe). Try again." -ForegroundColor Yellow
    }
}

Write-Host "KotOR co-op test build: setup" -ForegroundColor Cyan
$cfg = Read-Config $Config
$cfg["game"] = Ask-Folder "k1"
# KotOR II is optional: without it the game plays KotOR 1 with the KotOR HUD.
Write-Host "KotOR II is optional (it adds the KotOR II look and its feats, powers and classes)." -ForegroundColor DarkGray
$k2 = Ask-Folder "k2" $true
if ($k2) { $cfg["k2game"] = $k2 } else { $cfg.Remove("k2game") }
$name = ""
while (-not $name) { $name = Get-CleanName (Read-Host "Your name in co-op (what other players see)") }
$cfg["coop-name"] = $name
$hudPrompt = $(if ($k2) { "HUD style: k2 (KotOR II look) or k1 [k2]" } else { "HUD style: k1 (no KotOR II install, so the KotOR look is used) [k1]" })
$hud = Read-Host $hudPrompt
$cfg["hud"] = $(if ($k2 -and $hud -ne "k1") { "k2" } else { "k1" })
$server = Get-DefaultServer $Root
if ($server) { $cfg["coop-server"] = $server }
Write-Config $Config $cfg
Write-Host "Saved $Config" -ForegroundColor Green
if (-not $cfg["coop-server"]) {
    Write-Host "Co-op in this build is by IP:port (no join-code server). To host, forward UDP 27015 on your router and share your public IP and port." -ForegroundColor Yellow
}

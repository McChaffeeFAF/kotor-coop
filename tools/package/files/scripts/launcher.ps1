# One-click launcher: on first run it finds the games and writes reone.cfg by
# itself, then offers Host / Join / Play solo in a small window.
# Only reads the game folders; never writes to them.
. (Join-Path $PSScriptRoot "common.ps1")
. (Join-Path $PSScriptRoot "detect.ps1")
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()

# The primary display's modes in real pixels (EnumDisplaySettings is not
# DPI-scaled, unlike Screen.Bounds).
Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class KotorDisplay {
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct DEVMODE {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmDeviceName;
        public short dmSpecVersion, dmDriverVersion, dmSize, dmDriverExtra;
        public int dmFields, dmPositionX, dmPositionY, dmDisplayOrientation, dmDisplayFixedOutput;
        public short dmColor, dmDuplex, dmYResolution, dmTTOption, dmCollate;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmFormName;
        public short dmLogPixels;
        public int dmBitsPerPel, dmPelsWidth, dmPelsHeight, dmDisplayFlags, dmDisplayFrequency;
        public int dmICMMethod, dmICMIntent, dmMediaType, dmDitherType, dmReserved1, dmReserved2, dmPanningWidth, dmPanningHeight;
    }
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern bool EnumDisplaySettings(string device, int mode, ref DEVMODE dm);
    public static int[] Current() {
        var dm = new DEVMODE(); dm.dmSize = (short)Marshal.SizeOf(typeof(DEVMODE));
        if (!EnumDisplaySettings(null, -1, ref dm)) return new int[] { 1920, 1080 };
        return new int[] { dm.dmPelsWidth, dm.dmPelsHeight };
    }
    public static List<string> Modes() {
        var seen = new SortedSet<long>();
        var dm = new DEVMODE(); dm.dmSize = (short)Marshal.SizeOf(typeof(DEVMODE));
        for (int i = 0; EnumDisplaySettings(null, i, ref dm); i++) {
            if (dm.dmPelsWidth >= 800 && dm.dmPelsHeight >= 600) seen.Add((long)dm.dmPelsWidth * 100000 + dm.dmPelsHeight);
        }
        var list = new List<string>();
        foreach (var m in seen) list.Insert(0, (m / 100000) + " x " + (m % 100000));
        return list;
    }
}
"@
$desktop = [KotorDisplay]::Current()
$desktopText = "{0} x {1}" -f $desktop[0], $desktop[1]
$modes = @([KotorDisplay]::Modes())
if ($modes -notcontains $desktopText) { $modes = @($desktopText) + $modes }

# Automatic setup: keep what reone.cfg already has when it is still valid,
# fill in the rest.
$cfg = Read-Config $Config
$sources = @{}
foreach ($pair in @(@("k1", "game"), @("k2", "k2game"))) {
    $which, $key = $pair
    if (Test-Install $cfg[$key] $Games[$which].Exe) { $sources[$which] = "saved"; continue }
    $hit = Find-Install $which
    if ($hit) { $cfg[$key] = $hit.Path; $sources[$which] = $hit.Source } else { $cfg.Remove($key) }
}
if (-not $cfg["coop-name"]) { $cfg["coop-name"] = Get-CleanName $env:USERNAME }
if (-not $cfg["coop-name"]) { $cfg["coop-name"] = "Player" }
if ($cfg["hud"] -notin @("k1", "k2")) { $cfg["hud"] = "k2" }
# First run: fullscreen windowed at the desktop resolution.
if (-not $cfg.Contains("fullscreen") -and -not $cfg.Contains("borderless")) {
    $cfg["borderless"] = "1"; $cfg["fullscreen"] = "0"
}
if (-not $cfg["width"] -or -not $cfg["height"]) { $cfg["width"] = $desktop[0]; $cfg["height"] = $desktop[1] }
$server = Get-DefaultServer $Root
if ($server) { $cfg["coop-server"] = $server }
Write-Config $Config $cfg
# With a join-code service (official builds: make_tester_package.ps1 -Server)
# players share a code. Without one, the default, they join by IP:port and the
# host forwards its UDP port.
$useCodes = [bool]$cfg["coop-server"]
$hostPort = $DefaultCoopPort

# The window.
$form = New-Object System.Windows.Forms.Form
$form.Text = "KotOR Co-op (test build)"
$form.StartPosition = "CenterScreen"
$form.FormBorderStyle = "FixedDialog"
$form.MaximizeBox = $false
$form.ClientSize = New-Object System.Drawing.Size(560, $(if ($useCodes) { 366 } else { 382 }))
$form.Font = New-Object System.Drawing.Font("Segoe UI", 9)
$version = Join-Path $Root "VERSION.txt"
if (Test-Path $version) { $form.Text += " - " + ((Get-Content $version | Select-Object -First 1) -replace '^(build|release) ', '') }

function Add-Control($control, $x, $y, $w, $h) {
    $control.Location = New-Object System.Drawing.Point($x, $y)
    $control.Size = New-Object System.Drawing.Size($w, $h)
    $form.Controls.Add($control)
    return $control
}

$rows = @{}
$y = 14
foreach ($pair in @(@("k1", "game"), @("k2", "k2game"))) {
    $which, $key = $pair
    $label = Add-Control (New-Object System.Windows.Forms.Label) 14 $y 440 18
    $label.Font = New-Object System.Drawing.Font("Segoe UI", 9, [System.Drawing.FontStyle]::Bold)
    $label.Text = $Games[$which].Title
    $path = Add-Control (New-Object System.Windows.Forms.Label) 14 ($y + 19) 440 18
    $change = Add-Control (New-Object System.Windows.Forms.Button) 462 ($y + 6) 84 26
    $change.Text = "Change..."
    $change.Tag = @($which, $key)
    $change.Add_Click({
        $which, $key = $this.Tag
        $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
        $dialog.Description = "Select your $($Games[$which].Title) folder (the one with $($Games[$which].Exe))"
        if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { return }
        if (Test-Install $dialog.SelectedPath $Games[$which].Exe) {
            $cfg[$key] = $dialog.SelectedPath; $sources[$which] = "chosen"; Write-Config $Config $cfg; Update-Rows
        } else {
            [System.Windows.Forms.MessageBox]::Show("That folder has no chitin.key and $($Games[$which].Exe).", "Not a game folder") | Out-Null
        }
    })
    $rows[$which] = @{ Path = $path; Key = $key }
    $y += 50
}

$nameLabel = Add-Control (New-Object System.Windows.Forms.Label) 14 ($y + 4) 150 18
$nameLabel.Text = "Your name in co-op:"
$nameBox = Add-Control (New-Object System.Windows.Forms.TextBox) 170 $y 180 22
$nameBox.MaxLength = 24
$nameBox.Text = $cfg["coop-name"]
$hudLabel = Add-Control (New-Object System.Windows.Forms.Label) 364 ($y + 4) 40 18
$hudLabel.Text = "HUD:"
$hudBox = Add-Control (New-Object System.Windows.Forms.ComboBox) 404 $y 142 22
$hudBox.DropDownStyle = "DropDownList"
[void]$hudBox.Items.AddRange(@("KotOR II look", "KotOR look"))
$hudBox.SelectedIndex = $(if ($cfg["hud"] -eq "k1") { 1 } else { 0 })
$y += 36

$displayLabel = Add-Control (New-Object System.Windows.Forms.Label) 14 ($y + 4) 150 18
$displayLabel.Text = "Display:"
$displayBox = Add-Control (New-Object System.Windows.Forms.ComboBox) 170 $y 180 22
$displayBox.DropDownStyle = "DropDownList"
[void]$displayBox.Items.AddRange(@("Fullscreen windowed", "Fullscreen", "Windowed"))
$displayBox.SelectedIndex = $(if ($cfg["borderless"] -eq "1") { 0 } elseif ($cfg["fullscreen"] -eq "1") { 1 } else { 2 })
$resLabel = Add-Control (New-Object System.Windows.Forms.Label) 364 ($y + 4) 40 18
$resLabel.Text = "Size:"
$resBox = Add-Control (New-Object System.Windows.Forms.ComboBox) 404 $y 142 22
$resBox.DropDownStyle = "DropDownList"
[void]$resBox.Items.AddRange([object[]]$modes)
$saved = "{0} x {1}" -f $cfg["width"], $cfg["height"]
$resBox.SelectedIndex = [Math]::Max(0, $(if ($modes -contains $saved) { [array]::IndexOf($modes, $saved) } else { [array]::IndexOf($modes, $desktopText) }))
$displayBox.Add_SelectedIndexChanged({
    # Fullscreen windowed always covers the desktop at its own resolution.
    $resBox.Enabled = $displayBox.SelectedIndex -ne 0
    if ($displayBox.SelectedIndex -eq 0) { $resBox.SelectedIndex = [array]::IndexOf($modes, $desktopText) }
})
$resBox.Enabled = $displayBox.SelectedIndex -ne 0
$y += 40

$hostButton = Add-Control (New-Object System.Windows.Forms.Button) 14 $y 532 40
$hostButton.Text = $(if ($useCodes) { "Host a co-op game" } else { "Host a co-op game (UDP port $hostPort)" })
$hostButton.Font = New-Object System.Drawing.Font("Segoe UI", 11, [System.Drawing.FontStyle]::Bold)
$y += 50
$codeLabel = Add-Control (New-Object System.Windows.Forms.Label) 14 ($y + 8) 150 18
$codeLabel.Text = $(if ($useCodes) { "Join code from your host:" } else { "Host's IP:port:" })
$codeBox = Add-Control (New-Object System.Windows.Forms.TextBox) 170 ($y + 4) 180 22
if ($useCodes) { $codeBox.CharacterCasing = "Upper" }
$joinButton = Add-Control (New-Object System.Windows.Forms.Button) 364 $y 182 32
$joinButton.Text = $(if ($useCodes) { "Join" } else { "Join by IP:port" })
$y += 44
$soloButton = Add-Control (New-Object System.Windows.Forms.Button) 14 $y 170 30
$soloButton.Text = "Play solo"
$reportButton = Add-Control (New-Object System.Windows.Forms.Button) 376 $y 170 30
$reportButton.Text = "Report a bug"
$texgenButton = Add-Control (New-Object System.Windows.Forms.Button) 192 $y 176 30
$texgenButton.Text = "Generate enhanced textures"
$status = Add-Control (New-Object System.Windows.Forms.Label) 14 ($y + 38) 532 $(if ($useCodes) { 34 } else { 50 })

$TexgenExe = Join-Path $Root "texgen.exe"
$TexgenOut = Join-Path $Root "enhanced_textures"
$script:texgenProcess = $null
$script:texgenLog = $null

function Get-LastProgress([string]$Path) {
    # texgen prints "progress <done>/<total> <name> <outcome>" per texture; read the log while it is written.
    try {
        $stream = [System.IO.File]::Open($Path, "Open", "Read", "ReadWrite")
        $reader = New-Object System.IO.StreamReader($stream)
        $text = $reader.ReadToEnd(); $reader.Close()
    } catch { return $null }
    $lines = $text -split "`r?`n" | Where-Object { $_ }
    return $lines
}

function Format-TexgenState([string]$Path, [bool]$Finished, [int]$ExitCode) {
    $lines = @(Get-LastProgress $Path)
    $last = $lines | Where-Object { $_ -match '^progress (\d+)/(\d+) ' } | Select-Object -Last 1
    if (-not $Finished) {
        if ($last -match '^progress (\d+)/(\d+) ') { return "Generating enhanced textures: $($Matches[1]) of $($Matches[2])..." }
        return "Generating enhanced textures: reading your game files..."
    }
    if ($ExitCode -eq 0) { return "Done. Enhanced textures are in $TexgenOut. Surface Detail will use them next time you play." }
    $err = $lines | Where-Object { $_ -match 'FAILED|error|cannot' } | Select-Object -First 1
    return "Failed (exit code $ExitCode)." + $(if ($err) { " $err" } else { "" })
}

function Start-Texgen {
    $gamePath = $cfg["game"]
    if (-not (Test-Path $TexgenExe)) { $status.Text = "texgen.exe is missing from this folder."; return $null }
    if (-not ($gamePath -and (Test-Install $gamePath $Games["k1"].Exe))) { $status.Text = "Pick your KotOR folder first."; return $null }
    $script:texgenLog = Join-Path ([System.IO.Path]::GetTempPath()) ("kotor-texgen-" + [guid]::NewGuid().ToString("N") + ".log")
    $script:texgenProcess = Start-Process -FilePath $TexgenExe -WindowStyle Hidden -PassThru -WorkingDirectory $Root `
        -RedirectStandardOutput $script:texgenLog -RedirectStandardError ($script:texgenLog + ".err") `
        -ArgumentList @("--game", "`"$gamePath`"", "--out", "`"$TexgenOut`"")
    return $script:texgenProcess
}

function Update-Rows {
    $ready = $true
    foreach ($which in @("k1", "k2")) {
        $row = $rows[$which]
        $p = $cfg[$row.Key]
        if ($p -and (Test-Install $p $Games[$which].Exe)) {
            $row.Path.Text = "$p  ($($sources[$which]))"
            $row.Path.ForeColor = [System.Drawing.Color]::DarkGreen
        } else {
            $row.Path.Text = "Not found. Click Change... and pick the install folder."
            $row.Path.ForeColor = [System.Drawing.Color]::Firebrick
            $ready = $false
        }
    }
    foreach ($b in @($hostButton, $joinButton, $soloButton)) { $b.Enabled = $ready }
    # Surface Detail needs only KotOR 1's textures; texgen.exe ships beside engine.exe.
    $texgenButton.Enabled = ($null -eq $script:texgenProcess) -and (Test-Path $TexgenExe) -and
        [bool]($cfg["game"] -and (Test-Install $cfg["game"] $Games["k1"].Exe))
    if (-not $ready) { $status.Text = "Both games are needed: this build plays from your own KotOR and KotOR II installs." }
    elseif (-not $useCodes) { $status.Text = (Get-PortForwardHint $hostPort) + " The first time, allow engine.exe through Windows Firewall." }
    else { $status.Text = "Ready. The first time, allow engine.exe through Windows Firewall or co-op can't connect." }
}

function Start-Game([string]$Role, [string[]]$EngineArgs) {
    $name = Get-CleanName $nameBox.Text
    if (-not $name) { [System.Windows.Forms.MessageBox]::Show("Enter a name for co-op.", "Name") | Out-Null; return }
    $cfg["coop-name"] = $name
    $cfg["hud"] = $(if ($hudBox.SelectedIndex -eq 1) { "k1" } else { "k2" })
    $size = $(if ($displayBox.SelectedIndex -eq 0) { $desktop } else { $resBox.SelectedItem -split ' x ' })
    $cfg["width"] = $size[0]; $cfg["height"] = $size[1]
    $cfg["borderless"] = $(if ($displayBox.SelectedIndex -eq 0) { "1" } else { "0" })
    $cfg["fullscreen"] = $(if ($displayBox.SelectedIndex -eq 1) { "1" } else { "0" })
    Write-Config $Config $cfg
    Start-Engine ($EngineArgs + @("--coop-record", (Get-RecordPath $Role)))
    $form.Close()
}

$hostButton.Add_Click({ Start-Game "host" @("--host") })
$joinButton.Add_Click({
    $code = ($codeBox.Text -replace '[^A-Za-z0-9\-\.:]', '').Trim()
    if ($useCodes) {
        if (-not $code) { [System.Windows.Forms.MessageBox]::Show("Paste the join code your host sent (like K7QM-4TXR).", "Join code") | Out-Null; return }
    } else {
        $code = Resolve-JoinAddress $code $hostPort
        if (-not $code) { [System.Windows.Forms.MessageBox]::Show("Type your host's IP:port (like 203.0.113.7:$hostPort).", "Join by IP:port") | Out-Null; return }
    }
    Start-Game "join" @("--join", $code)
})
$codeBox.Add_KeyDown({ if ($_.KeyCode -eq "Enter" -and $joinButton.Enabled) { $joinButton.PerformClick() } })
$soloButton.Add_Click({ Start-Game "solo" @() })
$reportButton.Add_Click({
    Start-Process -FilePath "powershell" -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$(Join-Path $PSScriptRoot 'report.ps1')`"") -WorkingDirectory $Root
})

$texgenTimer = New-Object System.Windows.Forms.Timer
$texgenTimer.Interval = 500
$texgenTimer.Add_Tick({
    $p = $script:texgenProcess
    if (-not $p) { $texgenTimer.Stop(); return }
    if ($p.HasExited) {
        $texgenTimer.Stop()
        $code = $p.ExitCode
        $status.Text = Format-TexgenState $script:texgenLog $true $code
        $status.ForeColor = $(if ($code -eq 0) { [System.Drawing.Color]::DarkGreen } else { [System.Drawing.Color]::Firebrick })
        Remove-Item $script:texgenLog, ($script:texgenLog + ".err") -Force -ErrorAction SilentlyContinue
        $script:texgenProcess = $null
        $texgenButton.Enabled = $true
    } else {
        $status.Text = Format-TexgenState $script:texgenLog $false 0
    }
})
$texgenButton.Add_Click({
    if (Start-Texgen) {
        $texgenButton.Enabled = $false
        $status.ForeColor = [System.Drawing.SystemColors]::ControlText
        $status.Text = "Generating enhanced textures: reading your game files..."
        $texgenTimer.Start()
    }
})
$form.Add_FormClosing({ if ($script:texgenProcess -and -not $script:texgenProcess.HasExited) { try { $script:texgenProcess.Kill() } catch {} } })

Update-Rows
if ($env:KOTOR_LAUNCHER_SELFTEST) { $rows.Values | ForEach-Object { $_.Path.Text }; $status.Text; "host enabled: $($hostButton.Enabled)"; "co-op: $($hostButton.Text) | $($codeLabel.Text) | $($joinButton.Text)";"texgen button: $($texgenButton.Text) enabled=$($texgenButton.Enabled) exe=$(Test-Path $TexgenExe)"; "display: $($displayBox.SelectedItem) / $($resBox.SelectedItem) (desktop $desktopText; $($modes.Count) modes)"; return }
[void]$form.ShowDialog()

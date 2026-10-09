# Finds the KotOR and KotOR II installs. It only reads the registry and the
# game folders; it never writes to them. Shared by setup.ps1 and launcher.ps1.

$Games = @{
    k1 = @{ Title = "Knights of the Old Republic"; Exe = "swkotor.exe"; SteamApp = "32370"
            GogId = "1207666283"
            Names = @("swkotor", "Star Wars Knights of the Old Republic", "Star Wars - Knights of the Old Republic",
                # Localised store and installer folder names (French, German, Italian, Spanish, Polish).
                "Star Wars Chevaliers de l'Ancienne République", "Star Wars - Chevaliers de l'Ancienne République",
                "Star Wars Cavalieri della Vecchia Repubblica", "Star Wars - Cavalieri della Vecchia Repubblica",
                "Star Wars Caballeros de la Antigua República", "Star Wars - Caballeros de la Antigua República",
                "Star Wars Rycerze Starej Republiki", "Star Wars - Rycerze Starej Republiki") }
    k2 = @{ Title = "Knights of the Old Republic II"; Exe = "swkotor2.exe"; SteamApp = "208580"
            GogId = "1421404581"
            Names = @("Knights of the Old Republic II", "swkotor2", "Star Wars Knights of the Old Republic II",
                "Star Wars Knights of the Old Republic II - The Sith Lords", "Star Wars - Knights of the Old Republic II - The Sith Lords",
                "Star Wars Knights of the Old Republic II Die Sith Lords", "Star Wars - Knights of the Old Republic II - Die Sith Lords",
                "Star Wars Chevaliers de l'Ancienne République II Les Seigneurs Sith", "Star Wars - Chevaliers de l'Ancienne République II - Les Seigneurs Sith",
                "Star Wars Cavalieri della Vecchia Repubblica II I Signori dei Sith", "Star Wars - Cavalieri della Vecchia Repubblica II - I Signori dei Sith",
                "Star Wars Caballeros de la Antigua República II Los Señores Sith", "Star Wars - Caballeros de la Antigua República II - Los Señores Sith",
                "Star Wars Rycerze Starej Republiki II Lordowie Sithów", "Star Wars - Rycerze Starej Republiki II - Lordowie Sithów") }
}

function Test-Install([string]$Path, [string]$Exe) {
    return $Path -and (Test-Path -LiteralPath (Join-Path $Path "chitin.key")) -and (Test-Path -LiteralPath (Join-Path $Path $Exe))
}

function Get-RegValue([string]$Key, [string]$Name) {
    try { return (Get-ItemProperty -LiteralPath $Key -Name $Name -ErrorAction Stop).$Name } catch { return $null }
}

# Every Steam root: the registry's install path (wherever Steam lives), then
# the default folders.
function Get-SteamRoots {
    $roots = @(
        (Get-RegValue "HKCU:\Software\Valve\Steam" "SteamPath"),
        (Get-RegValue "HKLM:\SOFTWARE\WOW6432Node\Valve\Steam" "InstallPath"),
        (Get-RegValue "HKLM:\SOFTWARE\Valve\Steam" "InstallPath"),
        "${env:ProgramFiles(x86)}\Steam",
        "$env:ProgramFiles\Steam"
    )
    return $roots | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | ForEach-Object { ($_ -replace '/', '\').TrimEnd('\') } | Sort-Object -Unique
}

# Every Steam library's steamapps folder (libraryfolders.vdf lists the ones
# on other drives).
function Get-SteamApps {
    $apps = @()
    foreach ($steam in Get-SteamRoots) {
        $apps += (Join-Path $steam "steamapps")
        $vdf = Join-Path $steam "steamapps\libraryfolders.vdf"
        if (Test-Path -LiteralPath $vdf) {
            foreach ($m in [regex]::Matches((Get-Content -LiteralPath $vdf -Raw), '"path"\s+"([^"]+)"')) {
                $apps += (Join-Path ($m.Groups[1].Value -replace '\\\\', '\') "steamapps")
            }
        }
    }
    return $apps | Where-Object { Test-Path -LiteralPath $_ } | Sort-Object -Unique
}

# Candidate folders for one game, most reliable first. Each has a Source for
# the launcher to show.
function Get-Candidates([hashtable]$Game) {
    $found = @()
    foreach ($apps in Get-SteamApps) {
        # The app manifest names the real install folder, even a renamed one.
        $acf = Join-Path $apps ("appmanifest_{0}.acf" -f $Game.SteamApp)
        if (Test-Path -LiteralPath $acf) {
            $m = [regex]::Match((Get-Content -LiteralPath $acf -Raw), '"installdir"\s+"([^"]+)"')
            if ($m.Success) { $found += @{ Path = (Join-Path $apps ("common\" + $m.Groups[1].Value)); Source = "Steam" } }
        }
        foreach ($name in $Game.Names) { $found += @{ Path = (Join-Path $apps "common\$name"); Source = "Steam" } }
        # Any other folder of the library: a localised install can have any name; the chitin.key
        # and exe check in Find-Install is what decides.
        $common = Join-Path $apps "common"
        if (Test-Path -LiteralPath $common) {
            foreach ($dir in Get-ChildItem -LiteralPath $common -Directory -ErrorAction SilentlyContinue) { $found += @{ Path = $dir.FullName; Source = "Steam" } }
        }
    }
    # Steam's uninstall entry.
    foreach ($hive in @("HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall", "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall")) {
        $loc = Get-RegValue ("$hive\Steam App " + $Game.SteamApp) "InstallLocation"
        if ($loc) { $found += @{ Path = $loc; Source = "Steam" } }
    }
    # GOG Galaxy and the GOG offline installers register each game here.
    foreach ($gog in @("HKLM:\SOFTWARE\WOW6432Node\GOG.com\Games", "HKLM:\SOFTWARE\GOG.com\Games")) {
        if (-not (Test-Path $gog)) { continue }
        foreach ($key in Get-ChildItem $gog -ErrorAction SilentlyContinue) {
            $gameName = Get-RegValue $key.PSPath "gameName"
            $path = Get-RegValue $key.PSPath "path"
            # By GOG's product id (the same in every language), or by a game name in any of the
            # store's languages.
            $byId = $key.PSChildName -eq $Game.GogId
            $byName = $gameName -match 'Knights of the Old Republic|Chevaliers de l.Ancienne|Cavalieri della Vecchia|Caballeros de la Antigua|Rycerze Starej'
            if ($path -and ($byId -or ($gameName -and $byName))) { $found += @{ Path = $path; Source = "GOG" } }
        }
    }
    # GOG's default folders, and any game folder in them.
    foreach ($root in @("C:\GOG Games", "${env:ProgramFiles(x86)}\GOG Galaxy\Games", "$env:ProgramFiles\GOG Galaxy\Games", "${env:ProgramFiles(x86)}\LucasArts", "$env:ProgramFiles\LucasArts")) {
        if (-not (Test-Path -LiteralPath $root)) { continue }
        foreach ($dir in Get-ChildItem -LiteralPath $root -Directory -ErrorAction SilentlyContinue) { $found += @{ Path = $dir.FullName; Source = "GOG" } }
    }
    return $found
}

# The first candidate that is a real install (chitin.key plus the exe), or null.
function Find-Install([string]$Which) {
    $game = $Games[$Which]
    foreach ($c in Get-Candidates $game) {
        if (Test-Install $c.Path $game.Exe) { return @{ Path = (Resolve-Path -LiteralPath $c.Path).Path; Source = $c.Source } }
    }
    return $null
}

# reone.cfg as an ordered key -> value map, and back.
function Read-Config([string]$Path) {
    $cfg = [ordered]@{}
    if (Test-Path -LiteralPath $Path) {
        foreach ($line in Get-Content -LiteralPath $Path) {
            if ($line -match '^\s*([\w\-]+)\s*=\s*(.*)$') { $cfg[$Matches[1]] = $Matches[2].Trim() }
        }
    }
    return $cfg
}

function Write-Config([string]$Path, $Cfg) {
    Set-Content -LiteralPath $Path -Value ($Cfg.Keys | ForEach-Object { "$_=$($Cfg[$_])" }) -Encoding ascii
}

# The co-op service address the developer baked into this build.
function Get-DefaultServer([string]$Root) {
    $defaults = Join-Path $Root "coop-defaults.cfg"
    if (-not (Test-Path -LiteralPath $defaults)) { return $null }
    return (Read-Config $defaults)["coop-server"]
}

function Get-CleanName([string]$Name) {
    $Name = ($Name -replace '[^\w \-\.]', '').Trim()
    if ($Name.Length -gt 24) { $Name = $Name.Substring(0, 24) }
    return $Name
}

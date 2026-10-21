<#
Build the tester's portable co-op package: a zip with engine.exe, the DLLs
it needs, the launch scripts (Setup, Host, Join, ReportBug) and NO game
files. The zip is made locally only; nothing is uploaded or sent.

    powershell -ExecutionPolicy Bypass -File tools\package\make_tester_package.ps1
        [-Bin build\bin] [-Out build\package] [-Server coop.example.org:27200]

-Server is the join-code service address baked into coop-defaults.cfg
(Setup copies it into the player's reone.cfg); players then share join
codes. Leave it out and the package plays by IP:port only, the default.
-Release names a release (the folder and zip become KotOR-Coop-<release>).
#>
param(
    [string]$Bin = "",
    [string]$Out = "",
    [string]$Server = "",
    [string]$Release = ""
)
$ErrorActionPreference = "Stop"
$repo = Resolve-Path (Join-Path $PSScriptRoot "..\..")
if (-not $Bin) { $Bin = Join-Path $repo "build\bin" }
if (-not $Out) { $Out = Join-Path $repo "build\package" }
$files = Join-Path $PSScriptRoot "files"

$version = (& git -C $repo describe --always --dirty 2>$null)
if (-not $version) { $version = Get-Date -Format "yyyyMMdd" }
$name = if ($Release) { "KotOR-Coop-$Release" } else { "kotor-coop-tester-$version" }
$stage = Join-Path $Out $name
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null

# The engine and what it loads at run time. Test-only DLLs stay out.
Copy-Item (Join-Path $Bin "engine.exe") $stage
# The shader pack is rebuilt from glsl/ every time: the build doesn't
# regenerate it, and a stale copy in bin shipped old shaders once.
& (Join-Path $Bin "shaderpack.exe") (Join-Path $repo "glsl") $stage
if ($LASTEXITCODE -ne 0 -or -not (Test-Path (Join-Path $stage "shaderpack.erf"))) { throw "shaderpack.exe failed" }
Copy-Item (Join-Path $stage "shaderpack.erf") (Join-Path $Bin "shaderpack.erf") -Force
Get-ChildItem $Bin -Filter "*.dll" | Where-Object { $_.Name -notmatch '^(gtest|gmock)' } | Copy-Item -Destination $stage

# The NSS compiler is a developer tool, never part of the player package. texgen ships: the launcher's
# "Generate enhanced textures" button runs it, and it only reads the player's own game files.
Get-ChildItem $stage -Recurse -File | Where-Object { $_.Name -match 'nsscomp' } | Remove-Item -Force
$texgenExe = Join-Path $Bin "texgen.exe"
if (-not (Test-Path $texgenExe)) { throw "texgen.exe not found in $Bin (build with BUILD_TEXGEN=ON)" }
Copy-Item $texgenExe $stage

# The Visual C++ runtime, app-local (Microsoft allows redistributing the
# Redist folder's DLLs), so the tester needs no installer.
#
# It must match the toolset the engine was built with. A mismatched app-local
# msvcp140.dll/vcruntime140*.dll shadows the newer system runtime and the
# engine dies on startup (0xC0000005). VS's own folder search is alphabetical,
# not by version: it picked VS2019's VC142 runtime for a VC145 build once.
#
# The engine's imports name the toolset: boost_program_options-vc145-mt-x64...
# says vc145, so ask for that CRT first and fall back to the newest.
$engine = Join-Path $Bin "engine.exe"
$toolset = $null
if (Test-Path $engine) {
    $bytes = [System.IO.File]::ReadAllBytes($engine)
    $text = [System.Text.Encoding]::ASCII.GetString($bytes)
    $m = [regex]::Match($text, 'boost_program_options-vc(\d{3})-')
    if ($m.Success) { $toolset = "VC$($m.Groups[1].Value)" }
}
$vsRoots = @("$env:ProgramFiles\Microsoft Visual Studio", "${env:ProgramFiles(x86)}\Microsoft Visual Studio")
$candidates = foreach ($root in $vsRoots) {
    if (-not (Test-Path $root)) { continue }
    Get-ChildItem $root -Recurse -Directory -Filter "Microsoft.VC*.CRT" -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\Redist\\MSVC\\[^\\]+\\x64\\' } |
        Select-Object -ExpandProperty FullName
}
$crt = $null
if ($toolset) {
    $crt = $candidates | Where-Object { $_ -match "Microsoft\.$toolset\.CRT$" } | Sort-Object -Descending | Select-Object -First 1
}
if (-not $crt) {
    # No match (or no engine yet): the newest runtime works for older builds,
    # never the other way round.
    $crt = $candidates | Sort-Object -Descending | Select-Object -First 1
    if ($toolset -and $crt) {
        Write-Warning "No $toolset runtime found: using $crt, which may not match the engine's toolset."
    }
}
if ($crt) {
    Get-ChildItem $crt -Filter "*.dll" | Copy-Item -Destination $stage
    Write-Host "C++ runtime from $crt"
} else {
    Write-Warning "No Visual C++ Redist folder found: the tester needs the VC++ 2015-2026 x64 runtime installed."
}
# A CRT DLL copied from Bin (a developer's build folder) would win over the one
# chosen above and can be the wrong version; the redist folder is the source.
$binCrt = Get-ChildItem $Bin -Filter "*.dll" | Where-Object { $_.Name -match '^(msvcp140|vcruntime140|concrt140|vccorlib140)' }
if ($binCrt -and $crt) {
    Write-Host "note: Bin also has CRT DLLs; the redist copies stay authoritative"
    Get-ChildItem $crt -Filter "*.dll" | ForEach-Object { Copy-Item $_.FullName (Join-Path $stage $_.Name) -Force }
}

Copy-Item (Join-Path $files "*") $stage -Recurse
$defaults = @("# Written by make_tester_package.ps1. Setup copies coop-server into reone.cfg.")
if ($Server) { $defaults += "coop-server=$Server" }
Set-Content -Path (Join-Path $stage "coop-defaults.cfg") -Value $defaults -Encoding ascii
$versionLines = @()
if ($Release) { $versionLines += "release $Release" }
Set-Content -Path (Join-Path $stage "VERSION.txt") -Value ($versionLines + @("build $version", "made $(Get-Date -Format s)",
    ("engine.exe sha256 " + (Get-FileHash (Join-Path $stage "engine.exe") -Algorithm SHA256).Hash))) -Encoding ascii

# Guard: no game file may be in the package. Game data types by extension,
# and anything named like an install's key files.
$gameTypes = '\.(bif|key|tlk|rim|mod|erf|sav|tpc|tga|dds|txi|2da|mdl|mdx|wok|pwk|dwk|lyt|vis|git|are|ifo|dlg|ncs|nss|utc|utd|utp|uti|utm|ute|uts|utt|utw|jrl|fac|gui|ssf|wav|mp3|bik|lip)$'
$bad = Get-ChildItem $stage -Recurse -File | Where-Object {
    ($_.Name -match $gameTypes -and $_.Name -ne "shaderpack.erf") -or $_.Name -in @("chitin.key", "dialog.tlk", "swkotor.exe", "swkotor2.exe")
}
if ($bad) {
    $bad | ForEach-Object { Write-Error "game file in the package: $($_.FullName)" }
    throw "package refused: it would contain game files"
}
# shaderpack.erf is reone's own shader archive, built from glsl/ in this repo.
$foreign = Get-ChildItem $stage -Recurse -File | Where-Object { $_.Length -gt 200MB }
if ($foreign) { throw "unexpectedly large file in the package: $($foreign.Name)" }

$zip = Join-Path $Out "$name.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
# Entries are named with forward slashes by hand: Windows PowerShell's
# Compress-Archive and ZipFile both write backslashes, which unzip tools mangle.
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::Open($zip, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in Get-ChildItem $stage -Recurse -File) {
        $entry = $name + "/" + ($file.FullName.Substring($stage.Length + 1) -replace '\\', '/')
        [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName, $entry, [System.IO.Compression.CompressionLevel]::Optimal)
    }
} finally { $archive.Dispose() }
$size = [math]::Round((Get-Item $zip).Length / 1MB, 1)
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash
Write-Host "Package: $zip ($size MB, sha256 $hash)"
Get-ChildItem $stage -Recurse -File | ForEach-Object { $_.FullName.Substring($stage.Length + 1) }

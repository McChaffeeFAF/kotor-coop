# Bug report: zip the game's log, the co-op recordings and the settings.
# No game files and no saves go in. The zip stays here until you send it.
. (Join-Path $PSScriptRoot "common.ps1")
$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$work = Join-Path $env:TEMP "kotor-coop-report-$stamp"
New-Item -ItemType Directory -Force $work | Out-Null
$what = Read-Host "What happened? (one line, optional)"
Set-Content -Path (Join-Path $work "description.txt") -Value $what -Encoding utf8
foreach ($name in @("engine.log", "reone.cfg", "reone_settings.ini")) {
    $p = Join-Path $Root $name
    if (Test-Path $p) { Copy-Item $p $work }
}
if (Test-Path $Logs) {
    $dest = Join-Path $work "logs"
    New-Item -ItemType Directory -Force $dest | Out-Null
    Get-ChildItem $Logs -Filter "*.jsonl" | Sort-Object LastWriteTime -Descending | Select-Object -First 6 | Copy-Item -Destination $dest
}
$gpu = (Get-CimInstance Win32_VideoController -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name) -join "; "
$info = @(
    "report: $stamp",
    ("engine.exe sha256: " + (Get-FileHash $Engine -Algorithm SHA256).Hash),
    ("windows: " + [System.Environment]::OSVersion.VersionString),
    ("gpu: " + $gpu)
)
Set-Content -Path (Join-Path $work "system.txt") -Value $info -Encoding utf8
$reports = Join-Path $Root "reports"
New-Item -ItemType Directory -Force $reports | Out-Null
$zip = Join-Path $reports "bugreport_$stamp.zip"
Compress-Archive -Path (Join-Path $work "*") -DestinationPath $zip -Force
Remove-Item -Recurse -Force $work
Write-Host "Made $zip - send it to the developer." -ForegroundColor Green
Start-Process explorer.exe -ArgumentList ('/select,"' + $zip + '"')

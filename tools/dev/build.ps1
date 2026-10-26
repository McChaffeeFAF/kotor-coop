<#
.SYNOPSIS
  Fast Release build of reone: Ninja + sccache (shared cache) + the existing PCH.
.DESCRIPTION
  Enters the Visual Studio x64 developer environment, configures a separate
  build directory (default <repo>\build-ninja) with the vcpkg toolchain, builds
  and runs shaderpack (the create_shaderpack target does that as part of ALL).
  The MSBuild flow (cmake -S . -B build + cmake --build build --config Release)
  is untouched and still works.

  Default mode is sccache + no PCH (a warm shared cache beats the PCH: a fresh
  worktree rebuilds almost entirely from cache). -NoCache gives Ninja + PCH,
  the fastest mode when only one tree is built and there is no warm cache.

  Binaries land in <BuildDir>\bin  (engine.exe, tests.exe, texgen.exe,
  shaderpack.exe, shaderpack.erf, ...). Probes take --bin <BuildDir>\bin.

.EXAMPLE
  powershell -File tools\dev\build.ps1                       # build everything
  powershell -File tools\dev\build.ps1 -Target tests,engine  # subset
  powershell -File tools\dev\build.ps1 -NoCache -Clean       # cold, uncached
#>
param(
    [string]$BuildDir = "build-ninja",
    [string]$Config = "Release",
    [string[]]$Target = @(),
    [int]$Jobs = 0,                 # 0 = ninja default (cores + 2); use e.g. 6 when other worktrees build too
    [switch]$NoCache,               # do not use sccache (PCH then stays on)
    [switch]$NoPch,                 # no precompiled header (default when the cache is on: sccache cannot cache PCH compiles)
    [switch]$Pch,                   # force the PCH on together with the cache (cache then misses)
    [switch]$Clean,                 # delete the build dir first
    [switch]$ConfigureOnly,
    [string]$Root = $env:REONE_DEV_ROOT   # folder holding tools\vcpkg and the sccache folder
)
$ErrorActionPreference = "Stop"
$Target = @($Target | ForEach-Object { $_ -split "," } | Where-Object { $_ })   # -File passes a,b as one string
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not [System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $repo $BuildDir }

$vs = "C:\Program Files\Microsoft Visual Studio\18\Community"
$ninjaDir = "$Root\tools\vcpkg\downloads\tools\ninja-1.13.2-windows"
$sccacheDir = "$Root\tools\sccache-v0.19.0-x86_64-pc-windows-msvc"
$toolchain = "$Root/tools/vcpkg/scripts/buildsystems/vcpkg.cmake"

# VS x64 environment (vcvars64 via the dev shell script; not interactive)
& "$vs\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
$env:PATH = "$ninjaDir;$env:PATH"

# shared compile cache for every worktree
$useCache = -not $NoCache
if ($useCache) {
    $env:SCCACHE_DIR = "$Root\cache\sccache"
    $env:SCCACHE_CACHE_SIZE = "30G"
    $env:REONE_SCCACHE_DIR = $sccacheDir
    New-Item -ItemType Directory -Force $env:SCCACHE_DIR | Out-Null
}

$usePch = if ($Pch) { $true } elseif ($NoPch) { $false } else { -not $useCache }

if ($Clean -and (Test-Path $BuildDir)) { Remove-Item -Recurse -Force $BuildDir }

$cfg = @("-S", $repo, "-B", $BuildDir, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=$Config",
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain", "-DVCPKG_TARGET_TRIPLET=x64-windows",
    "-DVCPKG_MANIFEST_MODE=OFF", "-DVCPKG_INSTALLED_DIR=$Root/tools/vcpkg/installed",
    "-DBUILD_LAUNCHER=OFF", "-DBUILD_TOOLKIT=OFF", "-DBUILD_TESTS=ON", "-DBUILD_TEXGEN=ON",
    "-DENABLE_MOVIE=ON", "-DREONE_NETWORK=ON",
    "-DREONE_PCH=$(if ($usePch) {'ON'} else {'OFF'})",
    "-DREONE_COMPILER_CACHE=$(if ($useCache) {'ON'} else {'OFF'})")
& cmake @cfg
if ($LASTEXITCODE -ne 0) { throw "configure failed" }
if ($ConfigureOnly) { exit 0 }

$sw = [Diagnostics.Stopwatch]::StartNew()
$b = @("--build", $BuildDir)
if ($Target.Count) { $b += @("--target") + $Target }
if ($Jobs -gt 0) { $b += @("--parallel", $Jobs) } else { $b += "--parallel" }
& cmake @b
$code = $LASTEXITCODE
$sw.Stop()
if ($useCache) { & "$sccacheDir\sccache.exe" --show-stats | Select-String "Compile requests|Cache hits  |Cache misses  |Non-cacheable" }
"build $(if($code -eq 0){'OK'}else{'FAILED'}) in {0:n0} s -> {1}\bin" -f $sw.Elapsed.TotalSeconds, $BuildDir
exit $code

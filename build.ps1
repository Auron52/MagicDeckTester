<#
.SYNOPSIS
Canonical Windows build entry point for MagicDeckTester. The mirror of ./build.sh.

.DESCRIPTION
Produces an OPTIMIZED binary every time, into the same build/<Config>/ layout the Linux
build and every script in this repo expect (build/Release/mtg.exe, etc.).

Modes (optimized only):
  release          /O2            -> build/Release/          (DEFAULT; use this for almost everything)
  relwithdebinfo   /O2 + symbols  -> build/RelWithDebInfo/   (debugging a crash with a faithful stack)
  profile          /O2 + symbols  -> build/Profile/          (faithful profiling: Release codegen + symbols)

There is deliberately NO debug (/Od) mode, matching build.sh: an unoptimized build must be a
separate, deliberate route, never the default. See CLAUDE.md.

.EXAMPLE
.\build.ps1                          # release -> build/Release
.\build.ps1 relwithdebinfo
.\build.ps1 profile
.\build.ps1 release -- --target mtg  # forward extra args to the build step

.NOTES
Prefer build.cmd, which invokes this with -ExecutionPolicy Bypass. Windows client machines
default to a Restricted execution policy that blocks .ps1 files outright.
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$Mode = 'release',

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Rest
)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

function Fail($message) {
    Write-Host "build.ps1: $message" -ForegroundColor Red
    exit 1
}

# ---- mode ------------------------------------------------------------------------

switch ($Mode.ToLowerInvariant()) {
    'release'        { $cfg = 'Release' }
    'relwithdebinfo' { $cfg = 'RelWithDebInfo' }
    'symbols'        { $cfg = 'RelWithDebInfo' }
    'profile'        { $cfg = 'Profile' }
    { $_ -in '-h', '--help', 'help', '/?' } {
        Get-Help -Detailed $PSCommandPath
        exit 0
    }
    default {
        Write-Host "build.ps1: unknown mode '$Mode'" -ForegroundColor Red
        Write-Host "  allowed (optimized only): release (default), relwithdebinfo, profile"
        Write-Host "  most tasks just need:  .\build.cmd"
        exit 2
    }
}

# Any args after a literal `--` are forwarded to the build step (e.g. --target mtg).
$buildArgs = @()
if ($Rest -and $Rest.Count -gt 0 -and $Rest[0] -eq '--') {
    $buildArgs = $Rest[1..($Rest.Count - 1)]
}

# ---- prerequisites ---------------------------------------------------------------
# Check by NAME with an install pointer, rather than letting CMake's error surface: "CMAKE_CXX_COMPILER
# not set" tells a first-time user nothing about which of several things they are missing.

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Fail @"
cmake not found on PATH.
  Install "Visual Studio 2022 Build Tools" with the "Desktop development with C++"
  workload -- it bundles both the MSVC compiler and CMake:
    https://visualstudio.microsoft.com/downloads/  (scroll to "Tools for Visual Studio")
  Or via winget:  winget install Kitware.CMake Microsoft.VisualStudio.2022.BuildTools
  See docs/INSTALL.md.
"@
}

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Fail @"
git not found on PATH.
  The build downloads pugixml, nlohmann_json, doctest (and zlib, if your system has none)
  via CMake FetchContent, which shells out to git.
  Install:  winget install Git.Git
  See docs/INSTALL.md.
"@
}

# ---- stale single-config tree guard ----------------------------------------------
# build/<Config>/CMakeCache.txt means someone ran a bare `cmake -S . -B build\Release`, which
# leaves CMAKE_BUILD_TYPE empty -> NO optimization -> a silent ~10x slowdown. That tree also
# collides with the multi-config generator's output dir. Same guard as build.sh.

foreach ($c in 'Release', 'RelWithDebInfo', 'Profile') {
    $stale = Join-Path $PSScriptRoot "build\$c\CMakeCache.txt"
    if (Test-Path -LiteralPath $stale) {
        Fail @"
stale single-config CMake tree at build\$c\.
  build\<Config>\ is an OUTPUT directory of the multi-config generator, not a build tree.
  A CMakeCache.txt there came from a bare 'cmake -S . -B build\$c', which compiles with NO
  optimization (~10x slower). Delete it and re-run:
    Remove-Item -Recurse -Force build\$c
"@
    }
}

# ---- configure -------------------------------------------------------------------
# Pick the preset that matches the available toolchain. The Ninja preset is faster but needs
# cl.exe on PATH, which only a Developer Command Prompt (or VS's "Open Folder") provides.
# The 'windows' preset pins no Visual Studio version -- CMake selects the newest installed,
# so this works on 2019/2022/2026 alike.

$preset = 'windows'
if ((Get-Command ninja -ErrorAction SilentlyContinue) -and (Get-Command cl -ErrorAction SilentlyContinue)) {
    $preset = 'windows-ninja'
}

# Reuse the existing generator if the tree is already configured -- switching generators in
# place is an error, and re-picking one here would break an existing checkout.
if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'build\CMakeCache.txt')) {
    Write-Host ">>> reconfiguring build/ (existing generator)"
    & cmake -S . -B build | Out-Null
} else {
    Write-Host ">>> configuring build/ (preset: $preset)"
    & cmake --preset $preset | Out-Null
}
if ($LASTEXITCODE -ne 0) { Fail "cmake configure failed (exit $LASTEXITCODE)" }

# ---- build -----------------------------------------------------------------------

$jobs = $env:NUMBER_OF_PROCESSORS
if (-not $jobs) { $jobs = 4 }

Write-Host ">>> build: $cfg on $jobs jobs -> build/$cfg/"
& cmake --build build --config $cfg -j $jobs @buildArgs
if ($LASTEXITCODE -ne 0) { Fail "build failed (exit $LASTEXITCODE)" }

Write-Host ">>> done: build/$cfg/  (config=$cfg, optimized)"

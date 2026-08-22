<#
.SYNOPSIS
Start the MagicDeckTester play viewer -- the browser GUI where you play a game by hand.

.DESCRIPTION
The one command a new user needs. It:
  1. checks Node is installed (the viewer's local bridge is a dependency-free Node script),
  2. builds the optimized engine if build\Release\mtg.exe is missing,
  3. starts the server and opens http://localhost:8080 in your browser.

This is a single-user LOCAL dev tool: the server binds 127.0.0.1 and shells out to a local
binary. Do not expose it to a network.

.EXAMPLE
.\play.cmd                  # build if needed, serve, open a browser
.\play.cmd -NoOpen          # don't launch a browser (just print the URL)
$env:PORT=9000; .\play.cmd  # serve on a different port

.NOTES
Prefer play.cmd, which invokes this with -ExecutionPolicy Bypass. Windows client machines
default to a Restricted execution policy that blocks .ps1 files outright.
#>
[CmdletBinding()]
param(
    [switch]$NoOpen
)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

$port = $env:PORT
if (-not $port) { $port = '8080' }
$url = "http://localhost:$port"

# ---- 1. Node ---------------------------------------------------------------------
if (-not (Get-Command node -ErrorAction SilentlyContinue)) {
    Write-Host @"
play.ps1: 'node' not found on PATH.

The play viewer's local bridge (tools\play\server.js) is a Node script -- it has no npm
dependencies, but it does need the Node runtime.

  winget install OpenJS.NodeJS.LTS
  or download the LTS installer from https://nodejs.org/

See docs\INSTALL.md.
"@ -ForegroundColor Red
    exit 1
}

# ---- 2. engine -------------------------------------------------------------------
$bin = $null
foreach ($cand in 'build\Release\mtg.exe', 'build\Release\mtg') {
    if (Test-Path -LiteralPath (Join-Path $PSScriptRoot $cand)) { $bin = $cand; break }
}

if (-not $bin) {
    Write-Host ">>> engine not built yet -- building it now (this takes a few minutes the first time)"
    & (Join-Path $PSScriptRoot 'build.cmd')
    if ($LASTEXITCODE -ne 0) {
        Write-Host "play.ps1: build failed (exit $LASTEXITCODE) -- see the output above." -ForegroundColor Red
        exit 1
    }
}

# ---- 3. serve --------------------------------------------------------------------
if (-not $NoOpen) {
    # Open the browser shortly AFTER the server starts listening. Failure to open is not fatal --
    # the URL is printed either way.
    Start-Job -ScriptBlock { Start-Sleep -Seconds 1; Start-Process $using:url } | Out-Null
}

Write-Host ">>> starting the play viewer at $url   (Ctrl-C to stop)"
& node tools\play\server.js
exit $LASTEXITCODE

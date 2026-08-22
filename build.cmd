@echo off
REM Canonical Windows build entry point for MagicDeckTester -- the mirror of ./build.sh.
REM
REM This shim exists because Windows client machines default to a Restricted PowerShell
REM execution policy, which blocks .ps1 files outright. Routing through a .cmd with
REM -ExecutionPolicy Bypass means "build.cmd" just works from cmd.exe, from PowerShell,
REM and from a double-click in Explorer -- no policy fiddling by the user.
REM
REM   build.cmd                       release (optimized) -> build\Release
REM   build.cmd relwithdebinfo
REM   build.cmd profile
REM   build.cmd release -- --target mtg
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
exit /b %ERRORLEVEL%

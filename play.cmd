@echo off
REM Start the MagicDeckTester play viewer -- the browser GUI where you play a game by hand.
REM The mirror of ./play.sh.
REM
REM This shim exists because Windows client machines default to a Restricted PowerShell
REM execution policy, which blocks .ps1 files outright. Routing through a .cmd with
REM -ExecutionPolicy Bypass means "play.cmd" just works from cmd.exe, from PowerShell,
REM and from a double-click in Explorer.
REM
REM   play.cmd            build if needed, serve, open a browser
REM   play.cmd -NoOpen    don't launch a browser (just print the URL)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0play.ps1" %*
exit /b %ERRORLEVEL%

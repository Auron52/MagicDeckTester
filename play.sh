#!/usr/bin/env bash
# Start the MagicDeckTester play viewer -- the browser GUI where you play a game by hand.
#
# This is the one command a new user needs. It:
#   1. checks Node is installed (the viewer's local bridge is a dependency-free Node script),
#   2. builds the optimized engine if build/Release/mtg is missing,
#   3. starts the server and opens http://localhost:8080 in your browser.
#
# Usage:
#   ./play.sh                # build if needed, serve, open a browser
#   ./play.sh --no-open      # don't launch a browser (just print the URL)
#   PORT=9000 ./play.sh      # serve on a different port
#
# The Windows mirror is play.cmd (-> play.ps1).
#
# This is a single-user LOCAL dev tool: the server binds 127.0.0.1 and shells out to a local
# binary. Do not expose it to a network. (PLAY_HOST=0.0.0.0 exists for container port-forwarding.)
set -euo pipefail
cd "$(dirname "$0")"

open_browser=1
for arg in "$@"; do
  case "$arg" in
    --no-open) open_browser=0 ;;
    -h|--help)
      sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "play.sh: unknown option '$arg' (try --help)" >&2; exit 2 ;;
  esac
done

PORT="${PORT:-8080}"

# ---- 1. Node ---------------------------------------------------------------------
if ! command -v node >/dev/null 2>&1; then
  cat >&2 <<'EOF'
play.sh: 'node' not found on PATH.

The play viewer's local bridge (tools/play/server.js) is a Node script -- it has no npm
dependencies, but it does need the Node runtime.

  Debian/Ubuntu:  sudo apt install nodejs
  macOS:          brew install node
  Any platform:   https://nodejs.org/  (LTS)

See docs/INSTALL.md.
EOF
  exit 1
fi

# ---- 2. engine -------------------------------------------------------------------
# Same two-candidate probe the server and test harness use, so an .exe built under WSL/MSVC
# is still recognised.
BIN=""
for cand in build/Release/mtg build/Release/mtg.exe; do
  [ -f "$cand" ] && { BIN="$cand"; break; }
done

if [ -z "$BIN" ]; then
  echo ">>> engine not built yet -- building it now (this takes a few minutes the first time)"
  ./build.sh
  BIN=build/Release/mtg
fi

# ---- 3. serve --------------------------------------------------------------------
URL="http://localhost:${PORT}"

if [ "$open_browser" = "1" ]; then
  # Open the browser in the background AFTER a short delay, so the server is listening by the
  # time the page loads. Failure to open is not fatal -- the URL is printed either way.
  ( sleep 1
    if   command -v xdg-open >/dev/null 2>&1; then xdg-open "$URL"
    elif command -v open     >/dev/null 2>&1; then open "$URL"
    fi ) >/dev/null 2>&1 &
fi

echo ">>> starting the play viewer at $URL   (Ctrl-C to stop)"
exec node tools/play/server.js

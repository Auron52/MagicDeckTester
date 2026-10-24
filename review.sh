#!/usr/bin/env bash
# Start the reference REVIEW viewer: every saved reference game the shipped search beats, side by side
# with the search's line, where you mark each one as clairvoyance or "the search's line is better".
#
# Usage:
#   ./review.sh                 # serve http://localhost:8090
#   PORT=9001 ./review.sh
#
# The games come from `python3 scripts/ref_review.py build` (writes logs/ref_review/); re-run that after
# an engine change. Verdicts are saved to references/review/verdicts.json and protected like reference
# games (backup + references-autosave commit + push), so like ./play.sh this refuses to start from a
# temporary directory or a linked worktree.
set -euo pipefail
cd "$(dirname "$0")"
command -v node >/dev/null 2>&1 || { echo "review.sh: 'node' not found on PATH" >&2; exit 1; }
[ -f logs/ref_review/index.json ] || echo "review.sh: no games yet -- run: python3 scripts/ref_review.py build" >&2
exec node tools/replay/review_server.js

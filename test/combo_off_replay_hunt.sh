#!/usr/bin/env bash
# COMBO OFF REPLAY HUNT -- the phase-1 sweep's question, asked through the stateless
# --claude-play replay protocol instead of --scenario fixtures, so the states carry a REAL
# floating pool, the real shuffle, the real exile zone and real energy.
#
#   bash test/combo_off_replay_hunt.sh --quick   # 4 references + 12 seeds, horizon 6  (~10 s)
#   bash test/combo_off_replay_hunt.sh           # 11 references + 60 seeds, horizon 8  (~2 h)
#
# Results: logs/combo_off_hunt/results.json  (stable per-state ids, so a post-fix run diffs).
# Write-up: docs/design/combo-off-replay-hunt.md
#
# The full run writes logs/combo_off_hunt/results.json.partial every ten jobs; if it has to be
# cut short, promote that with `--finalise <partial> --out <results.json>` rather than losing it.
# Turn-7/8 frames on a developed board can spend >80 min inside ONE enumeration (see the HANG
# anomalies in the write-up) -- that is why --quick pins the horizon at 6.
#
# NEVER wrapped in a timeout (CLAUDE.md): a truncated hunt reads as a result.
set -euo pipefail
cd "$(dirname "$0")/.."

if [[ ! -x build/Release/mtg && ! -x build/Release/mtg.exe ]]; then
  echo "build/Release/mtg missing -- run ./build.sh first (never raw cmake)." >&2
  exit 2
fi

exec python3 test/combo_off_replay_hunt.py "$@"

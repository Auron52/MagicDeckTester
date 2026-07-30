#!/usr/bin/env bash
# One-off overnight chain: wait for the already-running TH R=40 gen, then run
# Slivers R=60 and Knights R=60 (all max_mull=6, depth=5 via the chunked+adaptive
# gen). Resumable: each deck's gen is itself resumable; re-running this script
# skips finished decks (the per-deck chunked gen no-ops once TARGET_R is reached).
# Non-destructive: writes only logs/<deck>_gen/. Commit frozen at launch HEAD.
set -uo pipefail
cd "$(dirname "$0")/.."
LOG=logs/mm6_chain.out
stamp(){ date -u +%Y-%m-%dT%H:%M:%SZ; }
say(){ echo "[$(stamp)] $*" | tee -a "$LOG"; }

say "mm6 chain start (HEAD=$(git rev-parse --short HEAD)). Waiting for TH R=40 to finish..."
# Wait until TH reached its target AND no analyzer is running (final pool/emit done).
while :; do
  done_th=$(grep -c 'target R=40 reached' logs/treasure_hunt_gen/chain.log 2>/dev/null || echo 0)
  busy=$(pgrep -x mtg-analyze | wc -l)
  if [ "$done_th" -ge 1 ] && [ "$busy" -eq 0 ]; then break; fi
  sleep 60
done
say "TH R=40 finished. Starting Slivers R=60."

KM_DECK=decks/slivers_vial/slivers_vial.txt KM_TARGET_R=60 KM_ROUND_R=5 \
  bash test/exhaustive_chunked_gen.sh
say "Slivers R=60 done (exit $?). Starting Knights R=60."

KM_DECK=decks/Knights/Knights.cod KM_TARGET_R=60 KM_ROUND_R=5 \
  bash test/exhaustive_chunked_gen.sh
say "Knights R=60 done (exit $?). mm6 chain COMPLETE."
say "Staged profiles: logs/slivers_vial_gen/pooled.profile.json, logs/Knights_gen/pooled.profile.json"
say "Validate each (KEEP + confounded BOTTOM A/B) before adopting."

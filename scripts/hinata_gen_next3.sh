#!/usr/bin/env bash
# Chain a SECOND batch of Hinata mulligan chunks after the first 5-chunk driver
# (scripts/hinata_gen_chunks.sh, driver PID passed as $1) finishes. Waits for that
# driver to exit so the two batches never oversubscribe the CPU (each chunk uses
# --threads 0 = all cores), then runs 3 more chunks on DISTINCT seeds 40001007-09
# (disjoint rollout streams, same frozen commit 7f3aaa8 -> same digest -> pools).
set -uo pipefail
cd /workspaces/MagicDeckTester2
OUT=logs/hinata_gen; mkdir -p "$OUT"
FIRST_PID="${1:-}"
if [[ -n "$FIRST_PID" ]]; then
  echo "=== [$(date -u +%H:%M:%S)] next3: waiting for first driver PID $FIRST_PID to finish ===" | tee -a "$OUT/driver.log"
  while kill -0 "$FIRST_PID" 2>/dev/null; do sleep 60; done
fi
# Belt-and-suspenders: ensure no Hinata analyze process is still running before we start.
while pgrep -f 'mtg-analyze decks/Hinata2' >/dev/null 2>&1; do sleep 60; done
echo "=== [$(date -u +%H:%M:%S)] next3: first batch clear; starting seeds 40001007-09 ===" | tee -a "$OUT/driver.log"
SEEDS="40001007 40001008 40001009" bash scripts/hinata_gen_chunks.sh

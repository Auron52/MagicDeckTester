#!/usr/bin/env bash
# Weekend Hinata mulligan-profile chunk generator. Runs N chunks SEQUENTIALLY (each --threads 0 = full cores,
# so no oversubscription), on the FROZEN commit hinata-gen-7f3aaa8. Each chunk is an independent disjoint
# rollout stream (distinct --seed) for cross-machine pooling; commit 7f3aaa8 checkpoints/resumes so a killed
# chunk restarts cleanly. Digest (bucket_fp/deck_fp/commit parity fingerprints) is identical across chunks and
# written to each sidecar header early. Settings per the user's exact command (d3, budget10, MAXMULL=6, R=1).
set -uo pipefail
cd /workspaces/MagicDeckTester2
OUT=logs/hinata_gen; mkdir -p "$OUT"
SEEDS="${SEEDS:-40001002 40001003 40001004 40001005 40001006}"
for SEED in $SEEDS; do
  echo "=== [$(date -u +%H:%M:%S)] chunk seed $SEED START ===" | tee -a "$OUT/driver.log"
  env MTG_KEEP_EXHAUSTIVE=1 MTG_EQUIV_DEPTH=3 MTG_EQUIV_BUDGET=10 MTG_EQUIV_PROBES=400 \
    MTG_EQUIV_THRESHOLD=0.01 MTG_EQUIV_SEED=20260701 MTG_KEEP_ROLLOUTS=1 MTG_KEEP_R_FLOOR=1 MTG_KEEP_MAXMULL=6 \
    MTG_KEEP_OUT_RAW="$OUT/chunk_s${SEED}.raw.json" \
    ./build/Release/mtg-analyze decks/Hinata2/Hinata2.cod --cards-json src/cards/data/cards.json \
    --max-turns 8 --seed "$SEED" --threads 0 > "$OUT/chunk_s${SEED}.log" 2>&1
  rc=$?
  echo "=== [$(date -u +%H:%M:%S)] chunk seed $SEED DONE rc=$rc (raw: $OUT/chunk_s${SEED}.raw.json) ===" | tee -a "$OUT/driver.log"
done
echo "=== [$(date -u +%H:%M:%S)] ALL CHUNKS DONE ===" | tee -a "$OUT/driver.log"

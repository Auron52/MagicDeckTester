#!/bin/bash
# General shuffle-variance ensemble: run a manifest across K salt realisations (fixed opening
# unless OPENING=1) and aggregate per-game outcomes. Usage:
#   shuffle_ensemble.sh <manifest.json> <K> <outdir> [OPENING]
# Writes <outdir>/salt<k>/a.wins for each salt; aggregate with aggregate_ensemble.py.
set -e
MAN=$1; K=$2; OUT=$3; OPENING=${4:-0}
mkdir -p "$OUT"
for k in $(seq 0 $((K-1))); do
  env MTG_SHUFFLE_SALT=$k $( [ "$OPENING" = 1 ] && echo MTG_SHUFFLE_SALT_OPENING=$k ) \
    build/Release/mtg --batch "$MAN" --game-log-dir "$OUT/salt$k" > "$OUT/salt$k.out" 2>&1
  echo "salt $k done: $(grep -hoE 'won=[0-9]+' "$OUT"/salt$k.out | head -1)"
done

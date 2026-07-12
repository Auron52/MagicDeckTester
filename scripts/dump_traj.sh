#!/bin/bash
# Accumulate teacher (NC K16 d2) executed-trajectory rows for the world-model, CHUNKED by seed-block so
# partial data is usable and the run is resumable (skips seed-blocks already recorded via a .done marker).
#   scripts/dump_traj.sh antilife decks/Anti-Lifegain.cod 10 40001 400
# args: name deckfile max_turns start_seed n_games
set -u
cd /workspaces/MagicDeckTester2
OUT=logs/model_improve/traj; mkdir -p "$OUT"
NM="$1"; DECK="$2"; MT="$3"; START="$4"; N="$5"
FILE="$OUT/${NM}.rows"; DONE="$OUT/${NM}.done"
touch "$DONE"
BLK=20  # games per chunk
end=$((START+N))
s=$START
while [ "$s" -lt "$end" ]; do
  if grep -qx "$s" "$DONE"; then s=$((s+BLK)); continue; fi
  MTG_DUMP_TRAJ="$OUT/_chunk_${NM}_${s}.rows" MTG_NC_SEARCH=1 MTG_NC_K=16 MTG_NC_DEPTH=2 \
    build/Release/mtg "$DECK" --games "$BLK" --seed "$s" --depth 1 --max-turns "$MT" --threads 12 >/dev/null 2>&1
  if [ -s "$OUT/_chunk_${NM}_${s}.rows" ]; then
    if [ ! -s "$FILE" ]; then cat "$OUT/_chunk_${NM}_${s}.rows" >> "$FILE"
    else tail -n +2 "$OUT/_chunk_${NM}_${s}.rows" >> "$FILE"; fi
    rm -f "$OUT/_chunk_${NM}_${s}.rows"
    echo "$s" >> "$DONE"
    echo "  [$NM] seed-block $s done; total rows=$(($(wc -l < "$FILE")-1)), games=$(( $(sort -u "$DONE" | grep -c .) * BLK ))"
  fi
  s=$((s+BLK))
done
echo "[$NM] DONE: $(($(wc -l < "$FILE")-1)) rows"

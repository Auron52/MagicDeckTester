#!/bin/bash
# Per-game wall scan for Melira at a suite-shaped config.
# Runs each game index gi of the 50-game d3 set as its own single-game process
# (`--seed $((BASE+gi))` == batch game gi of `--seed BASE --games 50`) and records
# ONLY the wall seconds (%e) plus the win turn, so nothing from stderr can leak
# into the number column.  Output: <out>/scan.tsv  (gi  seed  wall_s  win_turn)
# Usage: melira_wall_scan.sh <out_dir> [games=50] [depth=3] [budget=10] [base=1001] [par=8]
set -u
OUT=${1:?out dir}; N=${2:-50}; D=${3:-3}; B=${4:-10}; BASE=${5:-1001}; PAR=${6:-8}
BIN=${BIN:-./build/Release/mtg}
DECK=${DECK:-"decks/Melira Pod/Melira Pod.cod"}; PROF=${PROF:-"decks/Melira Pod/Melira Pod.profile.json"}
mkdir -p "$OUT/games"
one() {
  gi=$1; seed=$((BASE+gi))
  /usr/bin/time -f '%e' -o "$OUT/games/$gi.wall" \
    "$BIN" "$DECK" --profile "$PROF" --seed "$seed" --games 1 --depth "$D" --budget-ms "$B" --threads 1 \
    > "$OUT/games/$gi.out" 2> "$OUT/games/$gi.err"
  avg=$(grep -oE 'avg \(turns\)[^0-9]*[0-9.]+' "$OUT/games/$gi.out" | head -1 | grep -oE '[0-9.]+$')
  printf '%s\t%s\t%s\t%s\n' "$gi" "$seed" "$(tail -1 "$OUT/games/$gi.wall")" "${avg:-?}" > "$OUT/games/$gi.row"
}
export -f one; export OUT BASE BIN DECK PROF D B
seq 0 $((N-1)) | xargs -P "$PAR" -I{} bash -c 'one {}'
{ printf 'gi\tseed\twall_s\twin_turn\n'; sort -n "$OUT"/games/*.row; } > "$OUT/scan.tsv"
awk -F'\t' 'NR>1{s+=$3; if($3>m){m=$3; mg=$1}} END{printf "total=%.1f s  max=%.1f s (gi=%s)  n=%d\n", s, m, mg, NR-1}' "$OUT/scan.tsv"
sort -t$'\t' -k3,3nr "$OUT/scan.tsv" | head -12

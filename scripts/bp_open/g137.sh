#!/usr/bin/env bash
# Hinata2 shipped gi 137 single-game repro. Usage: g137.sh <bin-tag> <label> [extra mtg args...] [-- ENV=VAL ...]
# Writes build/scratch/bp_open/g137/<bin-tag>_<label>.{out,err}
set -u
P=/workspaces/MagicDeckTester
OUT=$P/build/scratch/bp_open/g137
TAG=$1; LBL=$2; shift 2
ARGS=(); ENVS=()
while [ $# -gt 0 ]; do
  if [ "$1" = "--" ]; then shift; ENVS=("$@"); break; fi
  ARGS+=("$1"); shift
done
mkdir -p "$OUT"
BIN=$P/build/scratch/bp_open/bin/$TAG/mtg
cd "$P"
env MTG_PGO=0 "${ENVS[@]}" nice -n 10 "$BIN" "decks/Hinata2/Hinata2.cod" --profile "decks/Hinata2/Hinata2.profile.json" \
  --games 1 --seed 6320138 --game-index 137 --threads 1 "${ARGS[@]}" > "$OUT/${TAG}_${LBL}.out" 2> "$OUT/${TAG}_${LBL}.err"
echo "rc=$? $(grep -m1 -i 'avg' "$OUT/${TAG}_${LBL}.out")" >> "$OUT/${TAG}_${LBL}.out"
tail -5 "$OUT/${TAG}_${LBL}.out"

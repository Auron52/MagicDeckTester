#!/usr/bin/env bash
# One game, one arm, with its game log, niced, --threads 1 (RULES item 4), from the staged data.
#   one_game.sh <out-dir> <deck-rel> <profile-rel> <seed> <gi> <extra mtg args...> -- [ENV=VAL ...]
# e.g. one_game.sh gi141_on "decks/Mirrorwing Dragon/Mirrorwing Dragon.cod" "decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json" 10660142 141 --budget-ms 20 -- MTG_BP_NEW_ONLY_DEMOTE=1
set -u
D=/workspaces/MagicDeckTester/build/scratch/bp_open/data
OUT=/workspaces/MagicDeckTester/build/scratch/bp_open/one/$1; DECK=$2; PROF=$3; SEED=$4; GI=$5; shift 5
ARGS=()
while [ $# -gt 0 ] && [ "$1" != "--" ]; do ARGS+=("$1"); shift; done
[ $# -gt 0 ] && shift
mkdir -p "$OUT"
cd "$D"
env MTG_DUMP_WINS=1 "$@" nice -n 10 /workspaces/MagicDeckTester/build/scratch/bp_open/bin/r2/mtg "$D/$DECK" --profile "$D/$PROF" \
  --seed "$SEED" --game-index "$GI" --games 1 --log-dir "$OUT/logs" --cards-json "$D/src/cards/data/cards.json" "${ARGS[@]}" > "$OUT/out.txt" 2> "$OUT/err.txt"
echo "rc=$?" >> "$OUT/out.txt"
grep -h "avg (turns)" "$OUT/out.txt"

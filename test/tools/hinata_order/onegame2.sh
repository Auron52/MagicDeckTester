#!/usr/bin/env bash
# onegame2.sh OUTDIR SEED GI HG(0/1) ORDER(0/1) [extra CLI args...] -- one game with a full per-game log, NOTHING
# on 9p (RULES item 9): cwd, binary, deck copy, cards.json and the log all on build/scratch/hinata-order.
# Single thread, niced (RULES item 4: single-game repros may run outside the batch lock).
S=/workspaces/MagicDeckTester/build/scratch/hinata-order
OUT=$1; SEED=$2; GI=$3; HG=$4; ORD=$5; shift 5
mkdir -p "$OUT"
cd "$S" || exit 9
envs=(MTG_DUMP_WINS=1 MTG_HINATA_ORDER_FULL="$ORD")
if [ "$HG" = 1 ]; then envs+=(MTG_START_LIFE=30 MTG_OPPONENT_HEADS=2); fi
env "${envs[@]}" nice -n 10 "${PROBE_BIN:-$S/bin/mtg.new2}" "$S/data/decks/Hinata2/Hinata2.cod" --cards-json "$S/data/cards/cards.json" \
    --seed "$SEED" --game-index "$GI" --games 1 --threads 1 --log-dir "$OUT" "$@" > "$OUT/out.txt" 2> "$OUT/err.txt"
echo "rc=$? $(grep -h '\[win\]' "$OUT/err.txt" | tail -1)"

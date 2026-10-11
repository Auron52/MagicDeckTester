#!/usr/bin/env bash
# vgame.sh <bin> <cell:d0|d3|ship|2hg> <gi> <seed> <outdir-on-volume> [ENV=VAL ...] : one Hinata2 game, JSON log
# + executor order trace in <outdir>. Runs through lowprio.sh.
set -u
BIN=$1; CELL=$2; GI=$3; SEED=$4; OUT=$5; shift 5
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8
D=$WT/decks/Hinata2/Hinata2.cod; P=$WT/decks/Hinata2/Hinata2.profile.json
mkdir -p "$OUT"
cd "$WT" || exit 1
args=("$D" --profile "$P" --seed "$SEED" --game-index "$GI" --games 1 --threads 1 --log-dir "$OUT")
extra=()
case $CELL in
  d0)   args+=(--depth 0 --ignore-play-profile);;
  2hg)  args+=(--depth 0 --ignore-play-profile); extra=(MTG_START_LIFE=30 MTG_OPPONENT_HEADS=2);;
  d3)   args+=(--depth 3 --budget-ms 10 --ignore-play-profile);;
  ship) args+=(--budget-ms 20);;
esac
env MTG_USER_ORDER_TRACE=1 "${extra[@]}" "$@" bash /workspaces/MagicDeckTester/build/scratch/lowprio.sh "$BIN" "${args[@]}" > "$OUT/run.txt" 2>&1
grep 'avg (turns)' "$OUT/run.txt" | awk '{print $4}'

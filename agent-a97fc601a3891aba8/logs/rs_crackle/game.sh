#!/usr/bin/env bash
# game.sh <bin> <cell:d0|d3|ship|2hg> <gi> <seed> <outdir> [ENV=VAL ...] : one Hinata2 game, JSON log in <outdir>.
# 2hg has no single-run flag here; it is run through a one-job manifest instead.
set -u
BIN=$1; CELL=$2; GI=$3; SEED=$4; OUT=$5; shift 5
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8
D=$WT/decks/Hinata2/Hinata2.cod; P=$WT/decks/Hinata2/Hinata2.profile.json
mkdir -p "$OUT"
cd "$WT" || exit 1
args=("$D" --profile "$P" --seed "$SEED" --game-index "$GI" --games 1 --threads 1 --log-dir "$OUT")
case $CELL in
  d0)   args+=(--depth 0 --ignore-play-profile);;
  d3)   args+=(--depth 3 --budget-ms 10 --ignore-play-profile);;
  ship) args+=(--budget-ms 20);;
esac
env MTG_PGO=0 "$@" nice -n 19 "$BIN" "${args[@]}" > "$OUT/run.txt" 2>&1
grep 'avg (turns)' "$OUT/run.txt"

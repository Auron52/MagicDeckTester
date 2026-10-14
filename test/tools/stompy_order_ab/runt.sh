#!/usr/bin/env bash
# Run one manifest with per-game traces. usage: runt.sh <binary> <manifest> <out_dir> [threads]
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9d0a1cbf3361f164
BIN=$1; M=$2; OUT=$3; TH=${4:-6}
mkdir -p "$OUT/wins" "$OUT/trace"
cd "$WT" || exit 1
MTG_DUMP_UNITS=1 taskset -c 0-5 nice -n 19 "$BIN" --batch "$M" --threads "$TH" \
    --cards-json "$WT/src/cards/data/cards.json" --game-log-dir "$OUT/wins" --game-trace-dir "$OUT/trace" \
    > "$OUT/batch.out" 2> "$OUT/batch.err"
echo "exit=$?"
grep -c played "$OUT/batch.out"

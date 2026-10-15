#!/usr/bin/env bash
# Single-game repros OUTSIDE the box lock (RULES.txt item 4: --threads 1, nice -n 10), optionally traced.
# usage: repro.sh <binary> <manifest> <out_dir> [trace]
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9d0a1cbf3361f164
BIN=$1; M=$2; OUT=$3
mkdir -p "$OUT/wins"
EXTRA=()
if [ "${4:-}" = "trace" ]; then mkdir -p "$OUT/trace"; EXTRA=(--game-trace-dir "$OUT/trace"); fi
cd "$WT" || exit 1
MTG_DUMP_UNITS=1 nice -n 10 "$BIN" --batch "$M" --threads 1 --cards-json "$WT/src/cards/data/cards.json" \
    --game-log-dir "$OUT/wins" "${EXTRA[@]}" > "$OUT/batch.out" 2> "$OUT/batch.err"
echo "exit=$?"
grep played "$OUT/batch.out"

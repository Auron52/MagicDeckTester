#!/usr/bin/env bash
# The reference bench (defaults vs the recommended levers) in ONE box-lock slot, then the comparison.
# usage: run_refs.sh <tag>
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9d0a1cbf3361f164
SC=/workspaces/MagicDeckTester/build/scratch/stompy-order
OUT=$SC/refs_$1
mkdir -p "$OUT"
date -u +"queued %FT%TZ bin=$(readlink -f "$WT/build/Release/mtg") head=$(git -C "$WT" rev-parse --short HEAD)" > "$OUT/run.info"
bash /workspaces/MagicDeckTester/build/scratch/boxlock/boxrun.sh batch bash "$WT/test/tools/stompy_order_ab/refs_inner.sh" "$OUT" \
    > "$OUT/lock.out" 2> "$OUT/lock.err"
cat "$OUT/run.info"
python3 "$WT/test/tools/stompy_order_ab/refs_cmp.py" "$OUT/A.txt" "$OUT/B.txt" > "$OUT/cmp.txt"
cat "$OUT/cmp.txt"

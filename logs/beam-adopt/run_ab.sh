#!/bin/bash
# Task 6: run both arms of the A/B back to back under ONE hold of the box lock (one big process at a time).
# usage: run_ab.sh <abdir>   (expects <abdir>/ab_B.json, ab_F.json, mtg.B, mtg.F; runs from the worktree root,
# where the binaries find src/cards/data/cards.json)
set -u
export D=$1
cd /workspaces/MagicDeckTester/.claude/worktrees/beam-staged || exit 1
rm -f "$D/rc.txt" "$D/DONE"
flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock bash -c '
  for arm in B F; do
    rm -rf "$D/wins_$arm"; mkdir -p "$D/wins_$arm"
    env MTG_PGO=0 MTG_RSS_CAP_GB=40 "$D/mtg.$arm" --batch "$D/ab_$arm.json" --threads 0 --game-log-dir "$D/wins_$arm" \
        > "$D/batch_$arm.out" 2> "$D/batch_$arm.err"
    echo "$arm rc=$?" >> "$D/rc.txt"
  done'
echo done > "$D/DONE"

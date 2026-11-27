#!/bin/bash
# Run the O (origin) and S (beam-staged2) manifests back to back under ONE hold of the box lock.
# usage: run.sh <abdir>   (expects <abdir>/ab_O.json, ab_S.json, mtg.O, mtg.S)
set -u
export D=$1
rm -f "$D/rc.txt" "$D/DONE"
flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock bash -c '
  for arm in O S; do
    [ -f "$D/ab_$arm.json" ] || continue
    if [ $arm = O ]; then cd /workspaces/MagicDeckTester/.claude/worktrees/k-fleet; else cd /workspaces/MagicDeckTester/.claude/worktrees/beam-staged2; fi
    rm -rf "$D/wins_$arm"; mkdir -p "$D/wins_$arm"
    env MTG_PGO=0 MTG_RSS_CAP_GB=40 "$D/mtg.$arm" --batch "$D/ab_$arm.json" --threads $(nproc) --game-log-dir "$D/wins_$arm" \
        > "$D/batch_$arm.out" 2> "$D/batch_$arm.err"
    echo "$arm rc=$?" >> "$D/rc.txt"
  done'
echo done > "$D/DONE"

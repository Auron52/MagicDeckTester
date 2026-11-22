#!/bin/bash
# ENUM-GAP final held-out A/B (off vs the winner, searched + d0), ONE pooled batch under the box lock, full width.
# usage: eg_ab3.sh <arm> <binary>
W=/workspaces/MagicDeckTester/.claude/worktrees/agent-a1651726d5b941608
D=$W/build/scratch/claude_merfolk_gate/ab_eg3
cd $W || exit 1
mkdir -p $D/logs
python3 logs/merfolk-gate/eg_manifest3.py $D/manifest.json "$1"
flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock env MTG_RSS_CAP_GB=40 MTG_PGO=0 \
  "$2" --batch $D/manifest.json --threads $(nproc) --game-log-dir $D/logs > $D/ab.out 2> $D/ab.err
echo rc=$? > $D/DONE

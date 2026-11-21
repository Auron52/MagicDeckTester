#!/bin/bash
# ENUM-GAP held-out A/B (off / a / b), ONE pooled batch under the box lock, full width. Binary = eg/mtg.v2.
W=/workspaces/MagicDeckTester/.claude/worktrees/agent-a1651726d5b941608
E=$W/build/scratch/claude_merfolk_gate/eg
D=$W/build/scratch/claude_merfolk_gate/ab_eg
cd $W || exit 1
mkdir -p $D/logs
python3 logs/merfolk-gate/eg_manifest.py $D/manifest.json "$@"
flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock env MTG_RSS_CAP_GB=40 \
  $E/mtg.v2 --batch $D/manifest.json --threads $(nproc) --game-log-dir $D/logs > $D/ab.out 2> $D/ab.err
echo rc=$? > $D/DONE

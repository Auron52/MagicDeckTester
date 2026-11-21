#!/bin/bash
# Held-out paired A/B (PD + PD 2HG), one pooled batch under the box lock.
S=/workspaces/MagicDeckTester/build/scratch/claude_pd_landdrop/ab
W=/workspaces/MagicDeckTester/.claude/worktrees/agent-aa57dd6400917806d
cd $W || exit 1
rm -f $S/DONE
cp build/Release/mtg $S/mtg.ab
exec 9>/workspaces/MagicDeckTester/build/scratch/claude_box.lock
echo "waiting for box lock $(date -u +%H:%M)" > $S/status
flock 9
echo "running $(date -u +%H:%M) threads=$(nproc)" >> $S/status
MTG_RSS_CAP_GB=40 $S/mtg.ab --batch $S/manifest.json --threads $(nproc) --game-log-dir $S/games > $S/batch.out 2> $S/batch.err
echo "rc=$? $(date -u +%H:%M)" >> $S/status
echo DONE > $S/DONE

#!/bin/bash
# After the A/B: smoke + regression (one pooled batch) under the box lock, in this worktree.
S=/workspaces/MagicDeckTester/build/scratch/claude_pd_landdrop
W=/workspaces/MagicDeckTester/.claude/worktrees/agent-aa57dd6400917806d
cd $W || exit 1
rm -f $S/tiers.DONE
while [ ! -f $S/ab/DONE ]; do sleep 30; done
exec 9>/workspaces/MagicDeckTester/build/scratch/claude_box.lock
echo "waiting for box lock $(date -u +%H:%M)" > $S/tiers.status
flock 9
echo "running $(date -u +%H:%M)" >> $S/tiers.status
MTG_PGO=0 MTG_RSS_CAP_GB=40 bash test/regression.sh --both > $S/tiers.log 2>&1
echo "rc=$? $(date -u +%H:%M)" >> $S/tiers.status
echo DONE > $S/tiers.DONE

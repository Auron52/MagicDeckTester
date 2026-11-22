#!/bin/bash
# Smoke + regression on the rebased ship tree (v6, HEAD 7d00b2961 on 4a7f65983), under the box lock.
cd /workspaces/MagicDeckTester/.claude/worktrees/agent-a1651726d5b941608 || exit 1
flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock env MTG_PGO=0 MTG_RSS_CAP_GB=40 bash test/regression.sh --both > /workspaces/MagicDeckTester/.claude/worktrees/agent-a1651726d5b941608/build/scratch/claude_merfolk_gate/eg/tiers6.out 2>&1
echo "rc=$?" > /workspaces/MagicDeckTester/.claude/worktrees/agent-a1651726d5b941608/build/scratch/claude_merfolk_gate/eg/TIERS6.DONE

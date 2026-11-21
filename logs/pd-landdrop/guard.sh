#!/bin/bash
# If the harness-tracked lane waiter (pid $1) dies before chain_diag started, re-queue chain_diag detached.
S=/workspaces/MagicDeckTester/build/scratch/claude_pd_landdrop
cd /workspaces/MagicDeckTester/.claude/worktrees/agent-aa57dd6400917806d || exit 1
while kill -0 "$1" 2>/dev/null; do sleep 30; done
if [ ! -e $S/chain_diag.status ]; then
  echo "requeued $(date -u +%H:%M)" > $S/guard.log
  bash scripts/box_lane.sh --slots 4 -- bash logs/pd-landdrop/chain_diag.sh >> $S/guard.log 2>&1
else
  echo "waiter exited, chain had started $(date -u +%H:%M)" > $S/guard.log
fi

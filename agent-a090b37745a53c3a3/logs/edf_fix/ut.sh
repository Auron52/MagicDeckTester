#!/bin/bash
# Run unit tests from the worktree root (cards.json is read relative to it), binary on the build volume.
# usage: ut.sh [filter]   (no filter = the whole suite)
W=/workspaces/MagicDeckTester/.claude/worktrees/agent-a090b37745a53c3a3
BIN=${UT_BIN:-/workspaces/MagicDeckTester/build/scratch/agent-a090b37745a53c3a3/mtg-test_final}
cd "$W" || exit 2
if [ -n "${1:-}" ]; then
  bash /workspaces/MagicDeckTester/build/scratch/lowprio.sh "$BIN" "-tc=$1" 2>&1 | grep -v "^\[enum-memo\|^\[solve-memo"
else
  bash /workspaces/MagicDeckTester/build/scratch/lowprio.sh "$BIN" 2>&1 | grep -v "^\[enum-memo\|^\[solve-memo" | tail -8
fi

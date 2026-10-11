#!/usr/bin/env bash
# vunit.sh [doctest args...] : the worktree's mtg-test (on the build volume) through lowprio.sh, from the worktree root.
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8
cd "$WT" || exit 1
bash /workspaces/MagicDeckTester/build/scratch/lowprio.sh "$WT/build/Release/mtg-test" "$@"
echo "exit=$?"

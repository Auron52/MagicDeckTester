#!/usr/bin/env bash
# unit.sh [doctest args...] : run the worktree's mtg-test at low priority from the worktree root.
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8
cd "$WT" || exit 1
env MTG_PGO=0 taskset -c 0-5 nice -n 19 "$WT/build/Release/mtg-test" "$@"
echo "exit=$?"

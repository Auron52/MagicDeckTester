#!/usr/bin/env bash
# unit.sh [test-case filter] -- run mtg-test from the worktree root (it resolves cards.json relative to it)
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6
cd "$WT" || exit 1
if [ -n "${1:-}" ]; then
  nice -n 19 "$WT/build/Release/mtg-test" "--test-case=$1" 2>&1 | tail -60
else
  nice -n 19 "$WT/build/Release/mtg-test" 2>&1 | tail -30
fi

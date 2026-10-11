#!/usr/bin/env bash
# unitfull2.sh [doctest filter] -- the unit binary from the build volume under lowprio.sh, run from the worktree root
# (it resolves cards.json relative to it); full output to build/scratch, failures + totals printed.
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6
S=/workspaces/MagicDeckTester/build/scratch/agent-aac6f548cc785c7e6
mkdir -p "$S"
cd "$WT" || exit 1
out="$S/unit_${2:-full}.log"
if [ -n "${1:-}" ]; then
  bash /workspaces/MagicDeckTester/build/scratch/lowprio.sh "$WT/build/Release/mtg-test" "--test-case=$1" > "$out" 2>&1
else
  bash /workspaces/MagicDeckTester/build/scratch/lowprio.sh "$WT/build/Release/mtg-test" > "$out" 2>&1
fi
grep -nE "ERROR|test cases|assertions|Status" "$out" | head -30

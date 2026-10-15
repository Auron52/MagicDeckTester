#!/usr/bin/env bash
# Every slower game of the resumed A/B, one at a time (single-game repros, --threads 1).
R=/workspaces/MagicDeckTester/.claude/worktrees/agent-a239469ed898c5c26/scripts/critter_order_ab/repro.sh
for g in "critter 1239 11261240 0" "critter 2384 11262385 0" "critter 4871 11264872 0" "critter 5562 11265563 0" \
         "critter 13045 11273046 0" "critter 15923 11275924 0" "critter2hg 210 11330211 3 10"; do
  echo "=== $g"
  bash "$R" $g | grep -v "^  t[0-9]* clean $" | grep -v "wins$"
done

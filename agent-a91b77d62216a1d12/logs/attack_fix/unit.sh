#!/usr/bin/env bash
# unit.sh <logfile> [doctest args...] -- the unit suite from the worktree root (it loads
# src/cards/data/cards.json relative to the source dir), binary from the build volume, lowprio.sh.
W=/workspaces/MagicDeckTester/.claude/worktrees/agent-a91b77d62216a1d12
LOG=$1; shift
cd "$W"
bash /workspaces/MagicDeckTester/build/scratch/lowprio.sh "$W/build/Release/mtg-test" "$@" > "$LOG" 2>&1
echo "exit=$?" >> "$LOG"

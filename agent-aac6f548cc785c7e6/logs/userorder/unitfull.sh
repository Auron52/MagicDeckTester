#!/usr/bin/env bash
# unitfull.sh <bin-dir-mtg-test> <out> -- the full unit suite, full output (failures included), from the worktree root
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6
cd "$WT" || exit 1
nice -n 19 "$1" > "$2" 2>&1
grep -nE "ERROR|FAILED|TEST CASE:|test cases" "$2" | grep -B2 -A2 -E "ERROR|test cases" | head -40

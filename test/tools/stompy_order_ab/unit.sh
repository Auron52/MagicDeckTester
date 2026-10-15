#!/usr/bin/env bash
# Unit tests under nice 10 (RULES item 4). usage: unit.sh [doctest filter]   (no filter = the full suite)
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9d0a1cbf3361f164
cd "$WT" || exit 1
LOG=/workspaces/MagicDeckTester/build/scratch/stompy-order/logs/unit_$(date -u +%H%M%S).log
if [ -n "${1:-}" ]; then nice -n 10 ./build/Release/mtg-test -tc="$1" > "$LOG" 2>&1
else nice -n 10 ./build/Release/mtg-test > "$LOG" 2>&1; fi
echo "exit=$?"
grep -E "test cases:|assertions:|Status:|ERROR|FAILED" "$LOG" | head -20

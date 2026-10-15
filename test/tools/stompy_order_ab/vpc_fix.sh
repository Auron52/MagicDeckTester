#!/usr/bin/env bash
# Reference replay sweep (every deck) under the fix binary with the levers given as env, through the box
# lock. usage: vpc_fix.sh <log> [ENV=VAL ...]
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9d0a1cbf3361f164
SC=/workspaces/MagicDeckTester/build/scratch/stompy-order
LOG=$1; shift
cd "$WT" || exit 1
env "$@" MTG_BIN="$SC/bin/mtg.fix" bash /workspaces/MagicDeckTester/build/scratch/boxlock/boxrun.sh batch \
    python3 test/viewer_protocol_check.py --strict --threads 20 > "$LOG" 2>&1
echo "exit=$?"
grep -E "^Viewer protocol:|play-drift|ENUM-GAP|BOARD-DIVERGED|enum-gap|board-diverged" "$LOG" | head -20

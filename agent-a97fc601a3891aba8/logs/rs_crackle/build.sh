#!/usr/bin/env bash
# Build Release in this worktree (low priority, 6 cores, no PGO). Usage: build.sh <logname>
set -u
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8
export MTG_PGO=0
taskset -c 0-5 nice -n 19 "$WT/build.sh" > "$WT/logs/rs_crackle/$1.log" 2>&1
echo "exit=$?"

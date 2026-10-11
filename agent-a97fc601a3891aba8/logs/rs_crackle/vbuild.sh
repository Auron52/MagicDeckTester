#!/usr/bin/env bash
# vbuild.sh <logname> : build this worktree through the shared slot script; log on the build volume.
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8
S=/workspaces/MagicDeckTester/build/scratch/agent-a97fc601a3891aba8
mkdir -p "$S"
export MTG_PGO=0
bash /workspaces/MagicDeckTester/build/scratch/wt_build.sh "$WT" > "$S/$1.log" 2>&1
echo "exit=$?"
tail -3 "$S/$1.log"
grep -n "error" "$S/$1.log" | head -20

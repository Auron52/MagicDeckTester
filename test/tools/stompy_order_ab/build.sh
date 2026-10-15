#!/usr/bin/env bash
# Build this worktree (Release) through the two-slot build gate and snapshot the binary.
# usage: build.sh <snapshot name under build/scratch/stompy-order/bin>
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9d0a1cbf3361f164
SC=/workspaces/MagicDeckTester/build/scratch/stompy-order
cd "$WT" || exit 1
bash /workspaces/MagicDeckTester/build/scratch/build2.sh > "$SC/logs/build_$1.log" 2>&1
rc=$?
echo "build rc=$rc"
tail -3 "$SC/logs/build_$1.log"
if [ $rc -eq 0 ]; then
  cp build/Release/mtg "$SC/bin/mtg.$1"
  echo "snapshot $SC/bin/mtg.$1 ($(git -C "$WT" rev-parse --short HEAD) + worktree)"
fi

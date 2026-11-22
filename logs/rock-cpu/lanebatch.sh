#!/bin/bash
# lanebatch.sh <tag> <binary> <manifest> [env...] : a small batch at 2 threads (run inside box_lane --slots 2)
S=/workspaces/MagicDeckTester/build/scratch/claude_rock_cpu; tag=$1; bin=$2; man=$3; shift 3
for e in "$@"; do export "$e"; done
cd /workspaces/MagicDeckTester/.claude/worktrees/rock-cpu; mkdir -p $S/logs_$tag
$bin --batch $man --threads 2 --game-log-dir $S/logs_$tag > $S/$tag.out 2> $S/$tag.err
echo "rc=$?" > $S/$tag.done

#!/bin/bash
# usage: ins.sh <tag> <bin> <manifest> [...]: sequential perf stat instructions:u runs at 2 threads (lane, 2 slots)
cd /workspaces/MagicDeckTester/.claude/worktrees/ds-apply5
C=/workspaces/MagicDeckTester/build/scratch/ds-apply5/cpu
while [ $# -ge 3 ]; do t=$1; b=$2; m=$3; shift 3
  MTG_PGO=0 MTG_BATCH_HEARTBEAT=0 MTG_SLOW_GAME_MS=0 perf stat -x, -e instructions:u -o $C/ins_$t.txt $b --batch $m --threads 2 > $C/ins_$t.out 2>/dev/null
  echo "$t $(grep instructions $C/ins_$t.txt | cut -d, -f1)" >> $C/ins_all.txt
done
echo done > $C/ins.DONE

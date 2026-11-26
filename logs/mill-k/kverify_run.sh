#!/bin/bash
# Pooled KF VERIFY batch under ONE hold of the box lock. usage: run.sh <dir>
set -u
D=$1; R=/workspaces/MagicDeckTester/.claude/worktrees/mill-k
rm -f $D/DONE $D/rc.txt
flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock bash -c "
  cd $R && date -u +%FT%TZ > $D/start.txt && env MTG_PGO=0 MTG_RSS_CAP_GB=40 MTG_LEAF_KILL_ONLY_VERIFY=1 build/Release/mtg-kv --batch $D/kv.json --threads \$(nproc) > $D/batch.out 2> $D/batch.err
  echo rc=\$? > $D/rc.txt"
echo done > $D/DONE

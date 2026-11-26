#!/bin/bash
# The pooled A/B under ONE hold of the box lock. usage: run.sh <abdir>
set -u
D=$1; R=/workspaces/MagicDeckTester/.claude/worktrees/mill-k
rm -f $D/DONE $D/rc.txt; rm -rf $D/wins; mkdir -p $D/wins
flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock bash -c "
  cd $R && env MTG_PGO=0 MTG_RSS_CAP_GB=40 build/Release/mtg-ab --batch $D/ab.json --threads \$(nproc) --game-log-dir $D/wins > $D/batch.out 2> $D/batch.err
  echo rc=\$? > $D/rc.txt"
echo done > $D/DONE

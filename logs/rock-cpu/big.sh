#!/bin/bash
# big.sh <tag> <binary> <manifest>  -- one pooled batch on the box lock, MTG_PGO irrelevant (explicit Release binary)
S=/workspaces/MagicDeckTester/build/scratch/claude_rock_cpu; tag=$1; bin=$2; man=$3
cd /workspaces/MagicDeckTester/.claude/worktrees/rock-cpu
echo "$tag waiting $(date -u)" > $S/$tag.status
flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock env MTG_RSS_CAP_GB=40 MTG_PGO=0 bash -c "
  echo \"$tag start \$(date -u)\" >> $S/$tag.status
  mkdir -p $S/logs_$tag
  $bin --batch $man --threads \$(nproc) --game-log-dir $S/logs_$tag > $S/$tag.out 2> $S/$tag.err
  echo \"$tag rc=\$? \$(date -u)\" >> $S/$tag.status"
echo DONE > $S/$tag.done

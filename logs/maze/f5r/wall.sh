#!/bin/bash
# usage: wall.sh <tag...>  -- one SEPARATE cold batch per binary mtg.<tag>, sequentially, inside one lock hold
S=/workspaces/MagicDeckTester/build/scratch/maze/f5r
cd /workspaces/MagicDeckTester/.claude/worktrees/maze-f5r
for t in "$@"; do
  /usr/bin/time -f "wall %e user %U" env MTG_RSS_CAP_GB=20 MTG_PGO=0 MTG_PAY_REFINE_STATS=1 MTG_OWED_ACTS_JOINT_STATS=1 $S/mtg.$t --batch $S/wall/m.json --threads $(nproc) > $S/wall/$t.out 2> $S/wall/$t.err
  echo "rc=$?" > $S/wall/$t.DONE
done

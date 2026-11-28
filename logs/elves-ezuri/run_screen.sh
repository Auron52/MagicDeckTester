#!/bin/bash
# Screen: off / ez (gated) / ezU (ungated) / full (MTG_ELVES_SCALING_PRUNE=0), BASE 8200000, 1000 std + 300 2HG, d5 b20.
S=/workspaces/MagicDeckTester/build/scratch/elves-ezuri
cd /workspaces/MagicDeckTester/.claude/worktrees/elves-ezuri || exit 1
rm -f $S/screen.DONE
BASE=8200000 N_STD=1000 N_2HG=300 python3 logs/elves-ezuri/mk_ab.py $S/screen.json || exit 1
MTG_RSS_CAP_GB=10 MTG_ELVES_SCALING_CENSUS=1 MTG_DUMP_WINS=1 bash scripts/box_lane.sh --slots 2 --cap-gb 5 -- \
  $S/mtg.ez2 --batch $S/screen.json --threads 2 > $S/screen.out 2> $S/screen.err
echo rc=$? >> $S/screen.out
touch $S/screen.DONE

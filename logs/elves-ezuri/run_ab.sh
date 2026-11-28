#!/bin/bash
# Held-out A/B: off / ez (gated) / ezU (ungated, measures the gate's misses), BASE 8400000, 4000 std + 1000 2HG, d5 b20.
S=/workspaces/MagicDeckTester/build/scratch/elves-ezuri
cd /workspaces/MagicDeckTester/.claude/worktrees/elves-ezuri || exit 1
rm -f $S/ab.DONE
BASE=8400000 N_STD=4000 N_2HG=1000 ARMS="off:MTG_ELVES_EZURI_COUNTS=0,ez:MTG_ELVES_EZURI_COUNTS=1,ezU:MTG_ELVES_EZURI_COUNTS=1;MTG_ELVES_EZURI_UNGATED=1" \
  python3 logs/elves-ezuri/mk_ab.py $S/ab.json || exit 1
MTG_RSS_CAP_GB=10 MTG_DUMP_WINS=1 bash scripts/box_lane.sh --slots 2 --cap-gb 5 -- \
  $S/mtg.ez3 --batch $S/ab.json --threads 2 > $S/ab.out 2> $S/ab.err
echo rc=$? >> $S/ab.out
touch $S/ab.DONE

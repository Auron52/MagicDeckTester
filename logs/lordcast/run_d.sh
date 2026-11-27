#!/bin/bash
# run_d.sh: amendment-D follow-ups on the mtg.r4 snapshot (2 threads; run under box_lane --slots 2).
# 1. the three searched games (s4.json, onB at d8 b0) under each amendment mask (MTG_LORDCAST_AMEND_DIAG: A=1 B=2 C=4 D=8)
# 2. the round-2 d0 slower games (r1.json: off vs onB) at mask 15 (all four amendments)
# 3. the d0 preview (pv.json: Boars/Goblins/Elves gi 0-9999, off vs onB) at mask 15. DONE marker $S/DONE_D.
S=/workspaces/MagicDeckTester/build/scratch/elves-lordcast; B=$S/mtg.r4
rm -f $S/DONE_D
export MTG_RSS_CAP_GB=4 MTG_DUMP_WINS=1
for m in 15 14 13 11 7 0; do
  MTG_LORDCAST_AMEND_DIAG=$m $B --batch $S/cls/s4.json --threads 2 > $S/cls/s4_m$m.out 2> $S/cls/s4_m$m.err
done
MTG_LORDCAST_AMEND_DIAG=15 $B --batch $S/cls/r1.json --threads 2 > $S/cls/r1_m15.out 2> $S/cls/r1_m15.err
MTG_LORDCAST_AMEND_DIAG=15 $B --batch $S/cls/pv.json --threads 2 > $S/cls/pv.out 2> $S/cls/pv.err
touch $S/DONE_D

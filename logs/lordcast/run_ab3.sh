#!/bin/bash
# Round 3: the AMENDED lever (MTG_ATTACK_LORD_CAST + MTG_LORDCAST_HAND + amendments A/B/C in code) vs off, on the
# round-2 seeds (BASE 7460000), Boars / Goblins / Elves: d0 50k, play 4000, 2HG play 1000. Queued on the heavy lock.
S=/workspaces/MagicDeckTester/build/scratch/elves-lordcast
W=/workspaces/MagicDeckTester/.claude/worktrees/elves-lordcast
cd $W || exit 1
rm -f $S/DONE3; echo $$ > $S/run_ab3.pid
cp build/Release/mtg $S/mtg.ab3 || exit 1
BASE=7460000 NULL=0 ARMS_D0="off:0:0,onB:1:1" ARMS_PLAY="off:0:0,onB:1:1" python3 logs/lordcast/mk_lordcast.py $S/m3.json boars goblins elves || exit 1
export MTG_RSS_CAP_GB=20 MTG_DUMP_WINS=1
flock /workspaces/MagicDeckTester/build/heavy.lock bash -c "echo started \$(date -u +%FT%TZ) > $S/ab3.STARTED; $S/mtg.ab3 --batch $S/m3.json > $S/ab3.out 2> $S/ab3.err; echo rc=\$? >> $S/ab3.out"
touch $S/DONE3

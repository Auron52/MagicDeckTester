#!/bin/bash
# Round 5: the amended lever (MTG_ATTACK_LORD_CAST; amendments A-E and the HAND fix all in code -- MTG_LORDCAST_HAND,
# MTG_LORDCAST_SITE and MTG_LORDCAST_STATS are DELETED, so the arms carry the one flag) vs off, on FRESH seeds
# (BASE 8100000; rounds 3 / 4 used 7700000 / 7900000), d0 50k, play 4000, 2HG play 1000. Binary = the WalkBoard
# scratch build (play-identical to round 4's mtg.ab4 by the digest check; only cheaper). Queued on the heavy lock.
# KEYS overrides the deck set (default the three that failed the bar; the other 19 lord decks: see mk_lordcast.py LORDS).
# Report: python3 logs/lordcast/report.py $S/ab5.err $S/ab5.out onB off --list
# d8 b0 of every searched slower game: python3 logs/lordcast/mk_d8.py $S/ab5.err $S/m5.json $S/m5_d8.json onB off
S=/workspaces/MagicDeckTester/build/scratch/elves-lordcast
W=/workspaces/MagicDeckTester/.claude/worktrees/elves-lordcast
cd $W || exit 1
rm -f $S/DONE5; echo $$ > $S/run_ab5.pid
cp build/Release/mtg $S/mtg.ab5 || exit 1
BASE=8100000 NULL=0 ARMS_D0="off:0,onB:1" ARMS_PLAY="off:0,onB:1" python3 logs/lordcast/mk_lordcast.py $S/m5.json ${KEYS:-boars goblins elves} || exit 1
# The arms must name ONLY MTG_ATTACK_LORD_CAST (the batch runner throws on the deleted MTG_LORDCAST_HAND).
python3 -c "import json,sys; f={k for j in json.load(open('$S/m5.json'))['jobs'] for k in j['flags']}; sys.exit(0 if f=={'MTG_ATTACK_LORD_CAST'} else 'bad flag set %s' % f)" || exit 1
export MTG_RSS_CAP_GB=20 MTG_DUMP_WINS=1
flock /workspaces/MagicDeckTester/build/heavy.lock bash -c "echo started \$(date -u +%FT%TZ) > $S/ab5.STARTED; $S/mtg.ab5 --batch $S/m5.json > $S/ab5.out 2> $S/ab5.err; echo rc=\$? >> $S/ab5.out"
touch $S/DONE5

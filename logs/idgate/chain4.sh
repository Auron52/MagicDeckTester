#!/usr/bin/env bash
# IDG-4 chain on wip/idgate-r2 (idgate replayed onto origin 1f3c23fab). EACH heavy stage takes the heavy lock
# separately (parent rule: never one heavy_run around a multi-stage chain).
set -u
W=/workspaces/MagicDeckTester/.claude/worktrees/idgate; cd $W
H="bash /workspaces/MagicDeckTester/scripts/heavy_run.sh"
S=/workspaces/MagicDeckTester/build/scratch/idgate; L=logs/idgate; M=$S/mem
export MTG_RSS_CAP_GB=20
date -u +%T > $L/chain4.start
MTG_BUILD_JOBS=12 $H ./build.sh > $L/build4.log 2>&1; rc=$?; echo "rc=$rc" > $L/build4.done
[ $rc -eq 0 ] || { echo "build failed" > $L/chain4.STOP; exit 1; }
./build/Release/mtg-test > $L/unit4_on.log 2>&1; echo "rc=$?" >> $L/unit4_on.log
MTG_ID_GATE_TRIAL=0 ./build/Release/mtg-test > $L/unit4_off.log 2>&1; echo "rc=$?" >> $L/unit4_off.log
# single-game memory check (1 thread): Merfolk Turns gi123, both arms (was > 12 GB on the old base)
for a in mft123 mft123off; do mkdir -p $M/$a.r2
  MTG_RSS_CAP_GB=12 /usr/bin/time -v -o $M/$a.r2/time.txt build/Release/mtg --batch $M/$a.json --threads 1 > $M/$a.r2/out 2> $M/$a.r2/err; done
echo done > $M/R2_SINGLE_DONE
# full-thread ovn-shape memory check (150 s of play), cap 20
sed -i 's/MTG_RSS_CAP_GB=14/MTG_RSS_CAP_GB=20/' $M/rep/run.sh
$H $M/rep/run.sh r2 ""
# smoke off arm vs GT (play-inertness of the trial code when off, on the rebased tree)
O=$S/smoke_off_r2; rm -rf $O; mkdir -p $O
$H bash $L/run_batch.sh $S/smoke_off.json $O
date -u +%T > $L/chain4.end

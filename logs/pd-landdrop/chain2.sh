#!/bin/bash
# chain2: variant 4 (castless plan -> no later-hand hold). Each step through the small lane.
S=/workspaces/MagicDeckTester/build/scratch/claude_pd_landdrop
W=/workspaces/MagicDeckTester/.claude/worktrees/agent-aa57dd6400917806d
BV=/workspaces/MagicDeckTester/build/wt/agent-aa57dd6400917806d
L="bash $W/scripts/box_lane.sh"
cd $W || exit 1
st() { echo "$1 $(date -u +%H:%M)" >> $S/chain2.status; }
rm -f $S/chain2.DONE
sleep 210
rm -f $BV/CMakeFiles/mtg-core.dir/Release/src/ai/ManaPayment.cpp.o $BV/CMakeFiles/mtg-test.dir/Release/test/unit/test_prevent_damage.cpp.o
if ! $L --slots 4 -- ./build.sh > $S/chain2.build.log 2>&1; then st "build FAIL"; echo FAIL > $S/chain2.DONE; exit 1; fi
st "build ok"
for f in 0 4; do
  $L --slots 1 -- env MTG_PD_HOLD_FIX=$f build/Release/mtg-test -tc="*later-hand Earthquake hold pays no pain*" > $S/ut2_f$f.log 2>&1; st "ut f$f rc=$?"
done
$L --slots 4 -- env MTG_MANA_ORACLE=2 MTG_PD_HOLD_FIX=4 build/Release/mtg --batch $W/logs/pd-landdrop/mc_seeds.json --threads 4 > $S/mc_f4.out 2> $S/mc_f4.err
st "mc f4 rc=$? applydrop=$(grep -c 'APPLY-DROP' $S/mc_f4.err)"
$L --slots 3 -- bash -c "
 REP_THREADS=1 bash $W/logs/pd-landdrop/rep.sh t_f4 $W/logs/pd-landdrop/g47case.json MTG_PD_HOLD_FIX=4 &
 REP_THREADS=1 bash $W/logs/pd-landdrop/rep.sh d8_f4 $W/logs/pd-landdrop/g47d8.json MTG_PD_HOLD_FIX=4 &
 REP_THREADS=1 bash $W/logs/pd-landdrop/rep.sh d8_off $W/logs/pd-landdrop/g47d8.json MTG_PD_LATER_HAND_HOLD=0 &
 wait"
st "games rc=$? f4=$(grep -h played $S/t_f4.out) d8f4=$(grep -h played $S/d8_f4.out) d8off=$(grep -h played $S/d8_off.out)"
echo DONE > $S/chain2.DONE

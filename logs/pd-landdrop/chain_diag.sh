#!/bin/bash
# Build (diag + temp selector + unit test), run the new unit test per variant, the T7 hold trace and T4 solve
# trace on gi47 (case settings), then gi47 case under each MTG_PD_HOLD_FIX variant.
S=/workspaces/MagicDeckTester/build/scratch/claude_pd_landdrop
W=/workspaces/MagicDeckTester/.claude/worktrees/agent-aa57dd6400917806d
BV=/workspaces/MagicDeckTester/build/wt/agent-aa57dd6400917806d
cd $W || exit 1
st() { echo "$1 $(date -u +%H:%M)" >> $S/chain_diag.status; }
rm -f $S/chain_diag.DONE
rm -f $BV/CMakeFiles/mtg-core.dir/Release/src/ai/ManaPayment.cpp.o $BV/CMakeFiles/mtg-test.dir/Release/test/unit/test_prevent_damage.cpp.o
if ! ./build.sh > $S/chain_diag.build.log 2>&1; then st "build FAIL"; echo FAIL > $S/chain_diag.DONE; exit 1; fi
st "build ok"
for f in 0 1 2; do
  MTG_PD_HOLD_FIX=$f build/Release/mtg-test -tc="*later-hand Earthquake hold pays no pain*" > $S/ut_t7_f$f.log 2>&1; st "ut_t7 f$f rc=$?"
done
mkdir -p $S/diag
MTG_PD_HOLD_TRACE_TURN=7 build/Release/mtg --batch $W/logs/pd-landdrop/g47case.json --threads 1 --game-log-dir $S/diag/wins7 > $S/diag_t7.out 2> $S/diag_t7.err; st "diag_t7 rc=$?"
MTG_TRACE_SOLVE=1 MTG_TRACE_SOLVE_TURN=4 build/Release/mtg --batch $W/logs/pd-landdrop/g47case.json --threads 1 --game-log-dir $S/diag/wins4 > $S/diag_t4.out 2> $S/diag_t4.err; st "diag_t4 rc=$?"
MTG_PD_LATER_HAND_HOLD=0 MTG_TRACE_SOLVE=1 MTG_TRACE_SOLVE_TURN=4 build/Release/mtg --batch $W/logs/pd-landdrop/g47case.json --threads 1 --game-log-dir $S/diag/wins4off > $S/diag_t4off.out 2> $S/diag_t4off.err; st "diag_t4off rc=$?"
for f in 1 2; do
  MTG_PD_HOLD_FIX=$f build/Release/mtg --batch $W/logs/pd-landdrop/g47case.json --threads 1 --game-log-dir $S/diag/case_f$f --game-trace-dir $S/diag/case_f$f/traces > $S/case_f$f.out 2> $S/case_f$f.err; st "case f$f rc=$?"
done
st "case stage done"
# mana-clean repro seeds of the hold and its follow-ups, oracle on, per variant (APPLY-DROP counts in .err)
for f in 0 1 2; do
  MTG_MANA_ORACLE=2 MTG_PD_HOLD_FIX=$f build/Release/mtg --batch $W/logs/pd-landdrop/mc_seeds.json --threads 4 > $S/mc_f$f.out 2> $S/mc_f$f.err
  st "mc f$f rc=$? applydrop=$(grep -c 'APPLY-DROP' $S/mc_f$f.err)"
done
# gi47 at d8 b0, f0 / f1 / f2 in parallel
for f in 0 1 2; do
  ( MTG_PD_HOLD_FIX=$f build/Release/mtg --batch $W/logs/pd-landdrop/g47d8.json --threads 1 --game-log-dir $S/diag/d8_f$f > $S/d8_f$f.out 2> $S/d8_f$f.err; st "d8 f$f rc=$?" ) &
done
wait
echo DONE > $S/chain_diag.DONE

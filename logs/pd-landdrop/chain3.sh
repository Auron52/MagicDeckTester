#!/bin/bash
# chain3 (ONE 4-slot lane job): build the final fix on the rebased tree, unit, mana-clean seeds, gi47 case + d8 b0 on/off.
S=/workspaces/MagicDeckTester/build/scratch/claude_pd_landdrop
W=/workspaces/MagicDeckTester/.claude/worktrees/agent-aa57dd6400917806d
F=MTG_PD_CASTLESS_LINE_NO_HOLD
cd $W || exit 1
st() { echo "$1 $(date -u +%H:%M)" >> $S/chain3.status; }
if ! MTG_BUILD_JOBS=4 ./build.sh > $S/chain3.build.log 2>&1; then st "build FAIL"; echo FAIL > $S/chain3.DONE; exit 1; fi
st "build ok"
build/Release/mtg-test -tc="Prevent Damage*" > $S/ut3_pd.log 2>&1; st "ut PD rc=$? $(grep 'test cases:' $S/ut3_pd.log)"
for a in 0 1; do
  env $F=$a MTG_MANA_ORACLE=2 build/Release/mtg --batch $W/logs/pd-landdrop/mc_seeds.json --threads 4 > $S/mc3_$a.out 2> $S/mc3_$a.err
  st "mc $F=$a rc=$? applydrop=$(grep -c 'APPLY-DROP' $S/mc3_$a.err)"
done
REP_THREADS=1 bash logs/pd-landdrop/rep.sh c3_on  logs/pd-landdrop/g47case.json $F=1 &
REP_THREADS=1 bash logs/pd-landdrop/rep.sh c3_off logs/pd-landdrop/g47case.json $F=0 &
REP_THREADS=1 bash logs/pd-landdrop/rep.sh d3_on  logs/pd-landdrop/g47d8.json $F=1 &
REP_THREADS=1 bash logs/pd-landdrop/rep.sh d3_off logs/pd-landdrop/g47d8.json $F=0 &
wait
st "games: $(grep -h played $S/c3_on.out $S/c3_off.out $S/d3_on.out $S/d3_off.out | tr '\n' ' ')"
build/Release/mtg-test > $S/ut3_all.log 2>&1; st "ut ALL rc=$? $(grep 'test cases:' $S/ut3_all.log)"
echo DONE > $S/chain3.DONE

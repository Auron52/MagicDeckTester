#!/bin/bash
# The bug-fix round's suite-wide pooled A/B: unit gate -> PGO build -> manifest -> ONE batch -> analysis.
set -u
R=/workspaces/MagicDeckTester
L=$R/logs/discard_impl
O=$L/ab3
mkdir -p $O
(cd $R; MTG_PGO=0 nice -n 10 ./build.sh > $L/build_ab3_rel.log 2>&1) || { echo "release build FAILED"; exit 1; }
nice -n 10 $R/build/Release/mtg-test > $L/unit_ab3.log 2>&1
grep -E "test cases|Status" $L/unit_ab3.log
grep -q "Status: SUCCESS" $L/unit_ab3.log || { echo "unit suite FAILED -- not running the A/B"; exit 1; }
(cd $R; nice -n 5 ./build.sh pgo > $L/build_ab3_pgo.log 2>&1) || echo "pgo build failed -- falling back to Release"
BIN=$R/build/Release/mtg
if [ "$(cat $R/build/PGO/SRC_TREE 2>/dev/null)" = "$(git -C $R rev-parse HEAD:src)" ] \
   && [ -z "$(git -C $R status --porcelain --untracked-files=no -- src)" ]; then BIN=$R/build/PGO/mtg; fi
python3 $L/make_ab3.py --out $O --edf-levers MTG_SOLVE_VERIFIED_WIN,MTG_EDF_GORGE_RED_SIZING,MTG_SOLVE_PENDING_AURA_EXACT
echo "ab3: binary $BIN, HEAD $(git -C $R rev-parse --short HEAD) src $(git -C $R rev-parse --short HEAD:src), start $(date -u +%FT%TZ)" > $O/run.info
MTG_DUMP_WINS=1 nice -n 10 $BIN --batch $O/manifest.json --threads 20 2> $O/batch.err > $O/batch.out
echo "ab3: exit $? end $(date -u +%FT%TZ)" >> $O/run.info
python3 $L/make_ab3.py --analyse $O/batch.err --edf-levers MTG_SOLVE_VERIFIED_WIN,MTG_EDF_GORGE_RED_SIZING,MTG_SOLVE_PENDING_AURA_EXACT > $O/analysis.txt
cat $O/run.info

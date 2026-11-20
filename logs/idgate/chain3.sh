#!/usr/bin/env bash
# IDG-3 chain (ONE heavy-lock hold): build the cold-estimate trial, units, memory probes (old vs new binary), sr2 A/B.
set -u
cd /workspaces/MagicDeckTester/.claude/worktrees/idgate
S=/workspaces/MagicDeckTester/build/scratch/idgate; L=logs/idgate
export MTG_RSS_CAP_GB=20 MTG_BUILD_JOBS=12
date -u +%T > $L/chain3.start
./build.sh > $L/build3.log 2>&1; rc=$?; echo "rc=$rc" > $L/build3.done
[ $rc -eq 0 ] || { echo "build failed" > $L/chain3.STOP; exit 1; }
cp build/Release/mtg $S/mem/mtg.new
./build/Release/mtg-test > $L/unit3_on.log 2>&1; echo "rc=$?" >> $L/unit3_on.log
MTG_ID_GATE_TRIAL=0 ./build/Release/mtg-test > $L/unit3_off.log 2>&1; echo "rc=$?" >> $L/unit3_off.log
OLD=$S/ovn1/mtg.bin; P=$S/mem/probe.sh
$P $OLD $S/mem/hin_on.json hin_on_old
$P $OLD $S/mem/hin_off.json hin_off_old
$P $OLD $S/mem/mft_on.json mft_on_old
$P $OLD $S/mem/mft_off.json mft_off_old
$P $S/mem/mtg.new $S/mem/hin_on.json hin_on_new
$P $OLD $S/mem/hin_off.json hin_off_old_t8 8      # DIAGNOSTIC thread count (memory scaling per game), not a run setting
echo done > $S/mem/PROBES_DONE
O=$S/sr2; rm -rf $O; mkdir -p $O
bash $L/run_batch.sh $L/sr_ab.json $O MTG_IDG_CENSUS=1
date -u +%T > $L/chain3.end

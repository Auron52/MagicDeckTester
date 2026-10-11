#!/bin/bash
# Game 671 both arms with the EDF projection / go-off-cut / hand-goff debug prints on (diagnosis only).
R=/workspaces/MagicDeckTester
O=$R/logs/discard_impl/rc/edf_d0
BIN=$R/build/PGO/mtg
for arm in off on; do
  ( cd "$R" ; MTG_EDF_GOFF_CUT_DEBUG=1 MTG_EDF_HAND_GOFF_DEBUG=1 MTG_EDF_GOFF_TRACE=1 MTG_EDF_GOFF_TRACE_N=400 MTG_DUMP_WINS=1 MTG_BATCH_HEARTBEAT=0 nice -n 19 "$BIN" --batch "$O/m_671_${arm}.json" --threads 1 ) > "$O/pj_671_${arm}.out" 2> "$O/pj_671_${arm}.err"
  echo "arm=$arm: $(grep -h 'wt=' "$O/pj_671_${arm}.err" | tail -1) lines=$(wc -l < "$O/pj_671_${arm}.err")"
done

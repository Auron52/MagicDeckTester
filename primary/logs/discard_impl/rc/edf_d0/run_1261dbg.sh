#!/bin/bash
# Games 1261 and 1424 both arms with EDF go-off / hand-goff / combo-route debug (diagnosis only).
R=/workspaces/MagicDeckTester
O=$R/logs/discard_impl/rc/edf_d0
BIN=$R/build/PGO/mtg
for gi in 1261 1424; do
for arm in off on; do
  ( cd "$R" ; MTG_TRACE=plans MTG_EDF_GOFF_CUT_DEBUG=1 MTG_EDF_HAND_GOFF_DEBUG=1 MTG_EDF_GOFF_TRACE=1 MTG_EDF_GOFF_TRACE_N=400 MTG_DUMP_WINS=1 MTG_BATCH_HEARTBEAT=0 nice -n 19 "$BIN" --batch "$O/m_${gi}_${arm}.json" --threads 1 ) > "$O/pj_${gi}_${arm}.out" 2> "$O/pj_${gi}_${arm}.err"
  echo "gi=$gi arm=$arm: $(grep -h 'wt=' "$O/pj_${gi}_${arm}.err" | tail -1) lines=$(wc -l < "$O/pj_${gi}_${arm}.err")"
done
done

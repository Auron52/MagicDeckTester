#!/bin/bash
# Both arms of the three slower games with the combo-route trace on (diagnosis only).
R=/workspaces/MagicDeckTester
O=$R/logs/discard_impl/rc/edf_d0
BIN=$R/build/PGO/mtg
for gi in 671 1261 1424; do
  for arm in off on; do
    ( cd "$R" ; MTG_EDF_COMBO_ROUTE_TRACE=1 MTG_DUMP_WINS=1 MTG_BATCH_HEARTBEAT=0 nice -n 19 "$BIN" --batch "$O/m_${gi}_${arm}.json" --threads 1 ) > "$O/cr_${gi}_${arm}.out" 2> "$O/cr_${gi}_${arm}.err"
    echo "gi=$gi arm=$arm: $(grep -h 'wt=' "$O/cr_${gi}_${arm}.err" | tail -1)  combo-route lines=$(grep -c combo-route "$O/cr_${gi}_${arm}.err")"
  done
done

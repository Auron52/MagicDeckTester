#!/bin/bash
# Game 671 both arms with the EDF loop trace on (diagnosis only; never branches game logic).
R=/workspaces/MagicDeckTester
O=$R/logs/discard_impl/rc/edf_d0
BIN=$R/build/PGO/mtg
for arm in off on; do
  ( cd "$R" ; MTG_EDF_LOOP_TRACE=1 MTG_DUMP_WINS=1 MTG_BATCH_HEARTBEAT=0 nice -n 19 "$BIN" --batch "$O/m_671_${arm}.json" --threads 1 ) > "$O/lt_671_${arm}.out" 2> "$O/lt_671_${arm}.err"
  echo "arm=$arm: $(grep -h 'wt=' "$O/lt_671_${arm}.err" | head -1)  lines=$(wc -l < "$O/lt_671_${arm}.err")"
done

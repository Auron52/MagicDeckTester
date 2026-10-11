#!/bin/bash
# Reproduce the 3 slower games (policy ON vs OFF) plus 2 faster ones, with full game logs.
R=/workspaces/MagicDeckTester
O=$R/logs/discard_impl/rc/edf_d0
BIN=$R/build/PGO/mtg
for spec in "671 217872" "1261 218462" "1424 218625" "189 217390" "982 218183"; do
  set -- $spec
  gi=$1; seed=$2
  for arm in off on; do
    if [ "$arm" = on ]; then fl=true; else fl=false; fi
    mf=$O/m_${gi}_${arm}.json
    cat > "$mf" <<EOF
{"jobs":[{"name":"g${gi}_${arm}","deck":"decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod","profile":"decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json","games":1,"seed":${seed},"game_index":${gi},"depth":0,"budget_ms":0,"ignore_play_profile":true,"flags":{"MTG_EDF_BUCKET_DISCARD":${fl}}}]}
EOF
    mkdir -p "$O/log_${gi}_${arm}"
    ( cd "$R" ; MTG_DUMP_WINS=1 MTG_BATCH_HEARTBEAT=0 nice -n 19 "$BIN" --batch "$mf" --threads 1 --game-log-dir "$O/log_${gi}_${arm}" --game-trace-dir "$O/log_${gi}_${arm}" ) > "$O/out_${gi}_${arm}.txt" 2> "$O/err_${gi}_${arm}.txt"
    echo "gi=$gi arm=$arm: $(grep -h 'wt=' "$O/err_${gi}_${arm}.txt" | head -1)"
  done
done

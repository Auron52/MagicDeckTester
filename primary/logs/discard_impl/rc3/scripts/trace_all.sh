#!/bin/bash
# For each game of a slower list: run both arms with EDF traces, then print the divergence report.
# usage: trace_all.sh <list> <deck-subdir>
R=/workspaces/MagicDeckTester/logs/discard_impl/rc3
LIST=$1; SUB=$2
TR="MTG_EDF_PROSPECTIVE_DEBUG=1 MTG_EDF_GOFF_TRACE_N=100000000 MTG_EDF_TURN_TRACE=1 MTG_EDF_GOFF_CUT_DEBUG=1 MTG_EDF_COMBO_ROUTE_TRACE=1"
while read -r deck cell gis olds news seeds; do
  gi=${gis#gi=}; seed=${seeds#seed=}
  [ -z "$gi" ] && continue
  base=$R/$SUB/tr_g${gi}_${cell}
  ENV_EXTRA="$TR" $R/scripts/run1.sh $deck $cell $gi $seed new ${base}_new &
  ENV_EXTRA="$TR" $R/scripts/run1.sh $deck $cell $gi $seed old ${base}_old &
  wait
  echo "################ $deck $cell gi=$gi seed=$seed listed $olds $news"
  python3 $R/scripts/divergence.py $R/$SUB/bisect/g${gi}_${cell}_new $R/$SUB/bisect/g${gi}_${cell}_old ${base}_new/stderr.txt ${base}_old/stderr.txt
done < "$LIST"

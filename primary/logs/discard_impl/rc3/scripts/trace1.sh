#!/bin/bash
# trace1.sh <deck> <cell> <gi> <seed> <tag> [extra env...]
# Runs new and old arm of one game with the EDF traces, into rc3/<deck>/trace_g<gi>_<cell>_<tag>_{new,old}
R=/workspaces/MagicDeckTester/logs/discard_impl/rc3
deck=$1; cell=$2; gi=$3; seed=$4; tag=$5; shift 5
export ENV_EXTRA="MTG_EDF_PROSPECTIVE_DEBUG=1 MTG_EDF_GOFF_TRACE_N=100000000 MTG_EDF_TURN_TRACE=1 MTG_EDF_GOFF_CUT_DEBUG=1 $*"
$R/scripts/run1.sh $deck $cell $gi $seed new $R/$deck/trace_g${gi}_${cell}_${tag}_new &
$R/scripts/run1.sh $deck $cell $gi $seed old $R/$deck/trace_g${gi}_${cell}_${tag}_old &
wait
for a in new old; do
  echo "== $a: $(grep avg $R/$deck/trace_g${gi}_${cell}_${tag}_$a/stdout.txt)"
done

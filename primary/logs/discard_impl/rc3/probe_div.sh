#!/usr/bin/env bash
# probe_div.sh <deck> <cell> <gi:turn>... : build a scenario at each game's divergent turn (from the OLD log)
# and sweep opponent life under old/new arms.
K=/workspaces/MagicDeckTester/logs/discard_impl/rc3
deck=$1; cell=$2; shift 2
mkdir -p $K/$deck/sc
for gt in "$@"; do
  gi=${gt%%:*}; t=${gt##*:}
  echo "################ $deck gi=$gi T$t"
  python3 $K/mkscen.py $deck $K/$deck/g${gi}_${cell}_old $t $K/$deck/sc/$gi.json ${MKARGS:-}
  python3 $K/scen_thresh.py $K/$deck/sc/$gi.json $K/$deck/sc/o$gi
done

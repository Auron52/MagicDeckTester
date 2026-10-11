#!/bin/bash
# Two 1261 fixtures with every trace stream on (diagnosis only).
R=/workspaces/MagicDeckTester
O=$R/logs/discard_impl/rc/edf_d0
BIN=$R/build/PGO/mtg
for b in s1261_on_t6 s1261_on_t6_no_disp; do
  ( cd "$R" ; MTG_TRACE=all MTG_EDF_GOFF_TRACE=1 MTG_EDF_GOFF_TRACE_N=400 nice -n 19 "$BIN" --scenario "$O/scen/$b.json" ) > "$O/scen/$b.tall.out" 2> "$O/scen/$b.tall.err"
  echo "== $b lines=$(wc -l < "$O/scen/$b.tall.err")"
done

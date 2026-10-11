#!/bin/bash
# Run every scenario fixture under scen/ (depth 0) and summarise the turn's actions.
R=/workspaces/MagicDeckTester
O=$R/logs/discard_impl/rc/edf_d0
BIN=$R/build/PGO/mtg
python3 -I "$O/mkscen.py"
for f in "$O"/scen2/s*.json; do
  b=$(basename "$f" .json)
  ( cd "$R" ; nice -n 19 "$BIN" --scenario "$f" ) > "$O/scen2/$b.out" 2> "$O/scen2/$b.err"
  echo "== $b exit=$?"
done

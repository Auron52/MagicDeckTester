#!/bin/bash
# Reproduce both arms of both games through --batch (one process per arm, single-threaded, niced).
D=/workspaces/MagicDeckTester/logs/discard_impl/rc/edf_search
B=/workspaces/MagicDeckTester/build/PGO/mtg
python3 "$D/mk.py"
cd /workspaces/MagicDeckTester || exit 1
for a in g63_off g63_on g163_off g163_on; do
  MTG_DUMP_WINS=1 MTG_SHED_STATS=1 nice -n 19 "$B" --batch "$D/$a.json" --threads 1 > "$D/$a.batch.out" 2> "$D/$a.batch.err" &
done
wait
grep -h "\[win\]" "$D"/*.batch.err

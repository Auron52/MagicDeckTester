#!/bin/bash
O=/workspaces/MagicDeckTester/logs/discard_impl/rc/edf_d0
for gi in 671 1261 1424 189 982; do
  for arm in off on; do
    python3 -I "$O/pp.py" "$O/log_${gi}_${arm}/g${gi}_${arm}_gi${gi}.json" > "$O/pp_${gi}_${arm}.txt"
  done
  diff "$O/pp_${gi}_off.txt" "$O/pp_${gi}_on.txt" > "$O/diff_${gi}.txt"
  echo "gi=$gi first diff line: $(head -1 "$O/diff_${gi}.txt")"
done

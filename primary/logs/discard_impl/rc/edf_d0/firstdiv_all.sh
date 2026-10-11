#!/bin/bash
O=/workspaces/MagicDeckTester/logs/discard_impl/rc/edf_d0
for gi in 671 1261 1424 189 982; do
  echo "=== gi=$gi"
  python3 -I "$O/firstdiv.py" "$O/log_${gi}_off/g${gi}_off_gi${gi}.json" "$O/log_${gi}_on/g${gi}_on_gi${gi}.json"
done

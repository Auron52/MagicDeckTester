#!/bin/bash
# st.sh seed:gi arm choices -> compact view of next decision
Q=1 python3 step.py $1 $2 "$3" 2>&1 | grep -v -E "^  (on_the_play|plan_contract|mdfc_backs|main_ordinal)" | head -${N:-16} | cut -c1-${W:-260}

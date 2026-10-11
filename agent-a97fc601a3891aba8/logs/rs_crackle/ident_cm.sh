#!/usr/bin/env bash
# Byte-identity for the two new levers:
#   Hinata, both levers =0 (env) on the new binary  vs  the 3d64d9a6 binary (no flags);
#   burn / dragonstorm / stompy at the new defaults (levers ON)  vs  the 3d64d9a6 binary.
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8
R=$WT/logs/rs_crackle
S=/workspaces/MagicDeckTester/build/scratch/agent-a97fc601a3891aba8
NEW=$WT/build/Release/mtg
OLD=$S/mtg_3d64d9a6
bash "$R/vbatch.sh" "$OLD" "$S/id_old" 3 &
bash "$R/vbatch.sh" "$NEW" "$S/id_new" 3 MTG_HINATA_CASTABLE_MANA=0 MTG_RESTRICTOR_ORDER_EXACT=0 &
wait
bash "$R/vbatch.sh" "$OLD" "$S/oth_old" 3 &
bash "$R/vbatch.sh" "$NEW" "$S/oth_new" 3 &
wait
echo "== Hinata, levers =0 vs 3d64d9a6"
python3 "$R/make_cm.py" ident "$S/id_old" "$S/id_new"
echo "== burn / dragonstorm / stompy, levers ON vs 3d64d9a6"
python3 "$R/make_cm.py" ident "$S/oth_old" "$S/oth_new"

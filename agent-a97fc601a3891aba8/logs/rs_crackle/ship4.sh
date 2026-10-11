#!/usr/bin/env bash
# Shipped gi 4 (lever R faster): turns + failed casts per arm.
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8
R=$WT/logs/rs_crackle
S=/workspaces/MagicDeckTester/build/scratch/agent-a97fc601a3891aba8
on=$(bash "$R/vgame.sh" "$S/mtg_final" ship 4 8320005 "$S/FR/ship_4_on")
off=$(bash "$R/vgame.sh" "$S/mtg_final" ship 4 8320005 "$S/FR/ship_4_off" MTG_RESTRICTOR_ORDER_EXACT=0)
echo "ship gi=4 on=$on off=$off"
echo "failed on:";  grep "paid=0" "$S/FR/ship_4_on/run.txt"  | cut -c1-160
echo "failed off:"; grep "paid=0" "$S/FR/ship_4_off/run.txt" | cut -c1-160
python3 "$R/casts.py" "$S/FR/ship_4_off" "$S/FR/ship_4_on" | cut -c1-200

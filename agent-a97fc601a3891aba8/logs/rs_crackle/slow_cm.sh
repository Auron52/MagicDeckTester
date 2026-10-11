#!/usr/bin/env bash
# Log both arms of the RESTRICTOR_ORDER_EXACT slower game (ship gi 68) and the faster ones, twice each
# for the budgeted cell (reproducibility).
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8
R=$WT/logs/rs_crackle
S=/workspaces/MagicDeckTester/build/scratch/agent-a97fc601a3891aba8
NEW=$WT/build/Release/mtg
for rep in 1 2; do
  on=$(bash "$R/vgame.sh" "$NEW" ship 68 8320069 "$S/SL/ship_68_on_$rep")
  off=$(bash "$R/vgame.sh" "$NEW" ship 68 8320069 "$S/SL/ship_68_off_$rep" MTG_RESTRICTOR_ORDER_EXACT=0)
  echo "ship gi=68 rep$rep: on=$on off=$off"
done

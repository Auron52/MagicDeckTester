#!/usr/bin/env bash
# 2HG gi 254 under B on: which lever makes turn 3 cast nothing?
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8
R=$WT/logs/rs_crackle
for v in "none" "MTG_HINATA_SUBSET_CREDIT=0" "MTG_EXEC_DROP_REPLAN=1" "MTG_IRENCRAG_WASTE=0" "MTG_EXEC_FEAS=0"; do
  ev=(MTG_HINATA_HOLD_NEEDS_HINATA=1 MTG_START_LIFE=30 MTG_OPPONENT_HEADS=2)
  [ "$v" != none ] && ev+=("$v")
  t=$(bash "$R/game.sh" "$R/mtg_hold1" d0 254 8130255 "$R/P254/$v" "${ev[@]}" | awk '{print $4}')
  echo "$v -> $t  T3: $(python3 "$R/casts.py" "$R/P254/$v" | grep '  T3 ' | cut -c1-120)"
done

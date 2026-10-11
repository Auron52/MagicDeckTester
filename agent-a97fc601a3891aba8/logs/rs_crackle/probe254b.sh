#!/usr/bin/env bash
# 2HG gi 254: is the turn-3 stall specific to B, or to any unheld lone Crackle?
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8
R=$WT/logs/rs_crackle
for v in "MTG_NO_HINATA_HOLD_CRACKLE=1" "MTG_NO_HINATA_HOLD_CRACKLE=1 MTG_HINATA_HOLD_LETHAL_BURN=0" "MTG_HINATA_HOLD_NEEDS_HINATA=1 MTG_HINATA_HOLD_LETHAL_BURN=0"; do
  read -r -a extra <<< "$v"
  ev=(MTG_START_LIFE=30 MTG_OPPONENT_HEADS=2 "${extra[@]}")
  d="$R/P254b/$(echo "$v" | tr ' =' '__')"
  t=$(bash "$R/game.sh" "$R/mtg_hold1" d0 254 8130255 "$d" "${ev[@]}" | awk '{print $4}')
  echo "$v -> $t"
  python3 "$R/casts.py" "$d" | sed -n '2,6p' | cut -c1-110
done

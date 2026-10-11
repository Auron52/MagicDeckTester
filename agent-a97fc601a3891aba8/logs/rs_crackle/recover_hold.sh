#!/usr/bin/env bash
# Per-game recovery of the class games (2cec6ec4's slower games) under the new levers.
#   pre   = MTG_CRACKLE_COUNT_EXACT=0 (the pre-2cec6ec4 turns the class games lost)
#   aoff  = 2cec6ec4 behaviour (HOLD_LETHAL_BURN=0)
#   base  = shipped defaults (HOLD_LETHAL_BURN on)
#   bon   = HOLD_NEEDS_HINATA=1
# -> logs/rs_crackle/LR/<cell>_<gi>_<arm>/
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8
R=$WT/logs/rs_crackle
BIN=$1
while read -r cls cell gi seed; do
  extra=(); c=$cell
  if [ "$cell" = 2hg ]; then extra=(MTG_START_LIFE=30 MTG_OPPONENT_HEADS=2); c=d0; fi
  line="$cls $cell gi=$gi:"
  for arm in pre aoff base bon; do
    case $arm in
      pre)  ev=(MTG_CRACKLE_COUNT_EXACT=0 MTG_HINATA_HOLD_LETHAL_BURN=0);;
      aoff) ev=(MTG_HINATA_HOLD_LETHAL_BURN=0);;
      base) ev=();;
      bon)  ev=(MTG_HINATA_HOLD_NEEDS_HINATA=1);;
    esac
    t=$(bash $R/game.sh "$BIN" $c $gi $seed $R/LR/${cell}_${gi}_${arm} "${ev[@]}" "${extra[@]}" | awk '{print $4}')
    line="$line $arm=$t"
  done
  echo "$line"
done <<'EOF'
A d0 157 7900158
A d0 196 7900197
B d0 8 7900009
B d0 212 7900213
B d0 324 7900325
B ship 36 7920037
B 2hg 90 7930091
EOF

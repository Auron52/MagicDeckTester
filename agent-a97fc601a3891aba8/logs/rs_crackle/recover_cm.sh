#!/usr/bin/env bash
# Per-game recovery of the d0-runner stall games under the conditions that exposed them.
# For each game x condition x arm: turns and the number of FAILED casts (uo-cast paid=0) in the trace.
#   cond: hold  = shipped defaults (hold on)
#         nhh   = MTG_HINATA_HOLD_NEEDS_HINATA=1 (where the stall was found)
#         nohold= MTG_NO_HINATA_HOLD_CRACKLE=1 (hold disabled entirely)
#   arm:  old (3d64d9a6 binary) | both (new, levers on) | roff (RESTRICTOR off) | moff (CASTABLE_MANA off)
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8
R=$WT/logs/rs_crackle
S=/workspaces/MagicDeckTester/build/scratch/agent-a97fc601a3891aba8
NEW=$WT/build/Release/mtg
OLD=$S/mtg_3d64d9a6
printf "%-10s %-7s %-6s %-6s %-6s %-6s   (turns/failed-casts)\n" game cond old both roff moff
while read -r cell gi seed; do
  for cond in hold nhh nohold; do
    case $cond in hold) ce=();; nhh) ce=(MTG_HINATA_HOLD_NEEDS_HINATA=1);; nohold) ce=(MTG_NO_HINATA_HOLD_CRACKLE=1);; esac
    line=$(printf "%-10s %-7s" "${cell}_$gi" "$cond")
    for arm in old both roff moff; do
      bin=$NEW; ae=()
      case $arm in old) bin=$OLD;; roff) ae=(MTG_RESTRICTOR_ORDER_EXACT=0);; moff) ae=(MTG_HINATA_CASTABLE_MANA=0);; esac
      out=$S/REC/${cell}_${gi}_${cond}_${arm}
      t=$(bash "$R/vgame.sh" "$bin" "$cell" "$gi" "$seed" "$out" "${ce[@]}" "${ae[@]}")
      f=$(grep -c "paid=0" "$out/run.txt")
      line="$line $(printf "%-6s" "${t%%.*}/$f")"
    done
    echo "$line"
  done
done <<'EOF'
2hg 254 8130255
d0 195 8100196
d0 865 8100866
d0 1773 8101774
EOF

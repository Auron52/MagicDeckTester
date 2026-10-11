#!/usr/bin/env bash
# The lever-R faster depth-0 games: turns and failed casts (paid=0) per arm, and the failed cards.
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8
R=$WT/logs/rs_crackle
S=/workspaces/MagicDeckTester/build/scratch/agent-a97fc601a3891aba8
while read -r cell gi seed; do
  on=$(bash "$R/vgame.sh" "$S/mtg_final" "$cell" "$gi" "$seed" "$S/FR/${cell}_${gi}_on")
  off=$(bash "$R/vgame.sh" "$S/mtg_final" "$cell" "$gi" "$seed" "$S/FR/${cell}_${gi}_off" MTG_RESTRICTOR_ORDER_EXACT=0)
  fon=$(grep "paid=0" "$S/FR/${cell}_${gi}_on/run.txt" | sed -E 's/.* t=([0-9]+) .*card=([^=]*) num.*/T\1:\2/' | tr '\n' ' ')
  foff=$(grep "paid=0" "$S/FR/${cell}_${gi}_off/run.txt" | sed -E 's/.* t=([0-9]+) .*card=([^=]*) num.*/T\1:\2/' | tr '\n' ' ')
  echo "$cell gi=$gi on=$on off=$off | failed on: [$fon] | failed off: [$foff]"
done <<'EOF'
2hg 13 8330014
2hg 257 8330258
d0 286 8300287
d0 1487 8301488
d0 1703 8301704
EOF

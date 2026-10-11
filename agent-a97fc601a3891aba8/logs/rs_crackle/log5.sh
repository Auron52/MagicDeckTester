#!/usr/bin/env bash
# Log the 5 rc3 games under: base new (nofix), base old, fix new. -> logs/rs_crackle/L5/<gi>_<arm>/
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8
R=$WT/logs/rs_crackle
OLD=(MTG_DRAGONSTORM_ORDER_OPAQUE=0 MTG_PAYABLE_ORDER_RITUALS=0 MTG_COLOR_EXACT_SCALED=0 MTG_ATTACK_TAP_NEED_EXACT=0
     MTG_ATTACK_TAP_ABILITY_DISCOUNT=0 MTG_ROLLOUT_LAND_ACT_COLOR=0 MTG_SOLVE_VERIFIED_WIN=0 MTG_EDF_GORGE_RED_SIZING=0
     MTG_SOLVE_PENDING_AURA_EXACT=0)
for pair in 1330763:762 1330874:873 1331155:1154 1331326:1325 1331550:1549; do
  seed=${pair%%:*}; gi=${pair##*:}
  echo "gi=$gi new_nofix: $(bash $R/game.sh $R/mtg_fix1 d0 $gi $seed $R/L5/${gi}_new_nofix MTG_CRACKLE_COUNT_EXACT=0)"
  echo "gi=$gi old_nofix: $(bash $R/game.sh $R/mtg_fix1 d0 $gi $seed $R/L5/${gi}_old_nofix MTG_CRACKLE_COUNT_EXACT=0 "${OLD[@]}")"
  echo "gi=$gi new_fix:   $(bash $R/game.sh $R/mtg_fix1 d0 $gi $seed $R/L5/${gi}_new_fix)"
done

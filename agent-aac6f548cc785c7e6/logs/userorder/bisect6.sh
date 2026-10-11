#!/usr/bin/env bash
# bisect6.sh <bin> -- the coordinator's 6 ab3-slower Dragonstorm d0 games under lever combinations.
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6
BIN="$1"
OLD="MTG_COLOR_EXACT_SCALED=0 MTG_ATTACK_TAP_NEED_EXACT=0 MTG_ATTACK_TAP_ABILITY_DISCOUNT=0 MTG_ROLLOUT_LAND_ACT_COLOR=0 MTG_SOLVE_VERIFIED_WIN=0 MTG_EDF_GORGE_RED_SIZING=0 MTG_SOLVE_PENDING_AURA_EXACT=0"
run() {  # seed gi envs...
  local seed="$1" gi="$2"; shift 2
  env "$@" nice -n 19 "$BIN" "$WT/decks/Dragonstorm/Dragonstorm.cod" --profile "$WT/decks/Dragonstorm/Dragonstorm.profile.json" \
      --seed "$seed" --game-index "$gi" --games 1 --depth 0 --ignore-play-profile --threads 1 2>/dev/null \
      | grep 'avg (turns)' | awk '{print $4}'
}
for pair in "445 1190446" "1011 1191012" "1412 1191413" "1590 1191591" "1686 1191687" "1701 1191702"; do
  set -- $pair; gi="$1"; seed="$2"
  head=$(run "$seed" "$gi" MTG_NONE=0)
  old=$(run "$seed" "$gi" $OLD MTG_DRAGONSTORM_ORDER_OPAQUE=0 MTG_PAYABLE_ORDER_RITUALS=0 MTG_DRAGONSTORM_USER_ORDER=0)
  o_user=$(run "$seed" "$gi" $OLD MTG_DRAGONSTORM_ORDER_OPAQUE=0 MTG_PAYABLE_ORDER_RITUALS=0)
  o_rit=$(run "$seed" "$gi" $OLD MTG_DRAGONSTORM_ORDER_OPAQUE=0 MTG_DRAGONSTORM_USER_ORDER=0)
  o_opq=$(run "$seed" "$gi" $OLD MTG_PAYABLE_ORDER_RITUALS=0 MTG_DRAGONSTORM_USER_ORDER=0)
  h_nouser=$(run "$seed" "$gi" MTG_DRAGONSTORM_USER_ORDER=0)
  h_norit=$(run "$seed" "$gi" MTG_PAYABLE_ORDER_RITUALS=0)
  h_noopq=$(run "$seed" "$gi" MTG_DRAGONSTORM_ORDER_OPAQUE=0)
  h_oldothers=$(run "$seed" "$gi" $OLD)
  echo "gi=$gi seed=$seed head=$head old=$old | old+USER=$o_user old+RIT=$o_rit old+OPQ=$o_opq | head-USER=$h_nouser head-RIT=$h_norit head-OPQ=$h_noopq head-others=$h_oldothers"
done

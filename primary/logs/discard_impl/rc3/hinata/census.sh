#!/usr/bin/env bash
# Census of exact-vs-plain walk disagreements on the Hinata d0 cell (2000 games, ab3 seeds), new arm, head binary.
ROOT=/workspaces/MagicDeckTester
OUT=/workspaces/MagicDeckTester/logs/discard_impl/rc3/hinata/census
mkdir -p "$OUT"
BIN=$ROOT/logs/discard_impl/rc3/mtg_head_ccacda4e
for arm in new old; do
  envs=(MTG_USER_ORDER_TRACE=1 MTG_PAYABLE_ORDER_STATS=1)
  if [ "$arm" = old ]; then envs+=(MTG_PAYABLE_ORDER_RITUALS=0 MTG_COLOR_EXACT_SCALED=0 MTG_ATTACK_TAP_NEED_EXACT=0 MTG_ATTACK_TAP_ABILITY_DISCOUNT=0 MTG_DRAGONSTORM_ORDER_OPAQUE=0 MTG_ROLLOUT_LAND_ACT_COLOR=0 MTG_SOLVE_VERIFIED_WIN=0 MTG_EDF_GORGE_RED_SIZING=0 MTG_SOLVE_PENDING_AURA_EXACT=0); fi
  ( cd "$ROOT" && env "${envs[@]}" nice -n 10 "$BIN" decks/Hinata2/Hinata2.cod --profile decks/Hinata2/Hinata2.profile.json \
      --seed 1330001 --game-index 0 --games 2000 --depth 0 --ignore-play-profile --threads 2 > "$OUT/d0_$arm.txt" 2>&1 )
  grep 'avg (turns)' "$OUT/d0_$arm.txt"
done

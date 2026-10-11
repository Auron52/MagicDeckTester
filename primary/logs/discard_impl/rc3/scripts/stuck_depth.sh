#!/bin/bash
# stuck_depth.sh <arm> <depth> -- snow gi=221 seed 1540222 at --depth D --budget-ms 20 (ignore play profile), units + wall
R=/workspaces/MagicDeckTester/logs/discard_impl/rc3
ARM=$1; D=$2
OUT=$R/snow/stuck221/depth${D}_$ARM
BIN=/workspaces/MagicDeckTester/logs/discard_impl/ab3/mtg_ab3_d6a760e5
OLDENV=""
if [ "$ARM" = old ]; then
  OLDENV="MTG_DRAGONSTORM_ORDER_OPAQUE=0 MTG_PAYABLE_ORDER_RITUALS=0 MTG_COLOR_EXACT_SCALED=0 MTG_ATTACK_TAP_NEED_EXACT=0 MTG_ATTACK_TAP_ABILITY_DISCOUNT=0 MTG_ROLLOUT_LAND_ACT_COLOR=0 MTG_SOLVE_VERIFIED_WIN=0 MTG_EDF_GORGE_RED_SIZING=0 MTG_SOLVE_PENDING_AURA_EXACT=0"
fi
mkdir -p "$OUT"
start=$(date +%s)
# shellcheck disable=SC2086
env $OLDENV MTG_DECISION_PROGRESS=1 MTG_TURN_CENSUS=$OUT/census.tsv nice -n 19 "$BIN" \
  /workspaces/MagicDeckTester/decks/Snow/Snow.cod --profile /workspaces/MagicDeckTester/decks/Snow/Snow.profile.json \
  --seed 1540222 --game-index 221 --games 1 --depth "$D" --budget-ms 20 --threads 1 --log-dir "$OUT" \
  > "$OUT/stdout.txt" 2> "$OUT/stderr.txt"
echo "exit=$? wall_s=$(( $(date +%s) - start ))" >> "$OUT/stdout.txt"

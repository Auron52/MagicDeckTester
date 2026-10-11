#!/usr/bin/env bash
# trace.sh <deck> <cell> <gi> <seed> <arm> <outfile> [ENV=VAL ...] : run one game with stderr diagnostics captured
set -u
ROOT=/workspaces/MagicDeckTester
BIN=${BIN:-$ROOT/logs/discard_impl/ab3/mtg_ab3_d6a760e5}
deck=$1; cell=$2; gi=$3; seed=$4; arm=$5; out=$6; shift 6
case $deck in
  hinata) D=$ROOT/decks/Hinata2/Hinata2.cod; P=$ROOT/decks/Hinata2/Hinata2.profile.json;;
  selesnya) D=$ROOT/decks/SelesnyaLifegain/SelesnyaLifegain.cod; P=$ROOT/decks/SelesnyaLifegain/SelesnyaLifegain.profile.json;;
esac
args=("$D" --profile "$P" --seed "$seed" --game-index "$gi" --games 1 --threads 1)
case $cell in
  d0) args+=(--depth 0 --ignore-play-profile);;
  d3) args+=(--depth 3 --budget-ms "${BUD:-10}" --ignore-play-profile);;
  ship) args+=(--budget-ms "${BUD:-20}");;
esac
envs=()
if [ "$arm" = old ]; then
  envs+=(MTG_DRAGONSTORM_ORDER_OPAQUE=0 MTG_PAYABLE_ORDER_RITUALS=0 MTG_COLOR_EXACT_SCALED=0 MTG_ATTACK_TAP_NEED_EXACT=0
         MTG_ATTACK_TAP_ABILITY_DISCOUNT=0 MTG_ROLLOUT_LAND_ACT_COLOR=0 MTG_SOLVE_VERIFIED_WIN=0 MTG_EDF_GORGE_RED_SIZING=0
         MTG_SOLVE_PENDING_AURA_EXACT=0)
fi
envs+=("$@")
cd "$ROOT" && env "${envs[@]}" nice -n 10 "$BIN" "${args[@]}" > "$out" 2>&1
grep 'avg (turns)' "$out"

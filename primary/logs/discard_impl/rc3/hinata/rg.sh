#!/usr/bin/env bash
# rg.sh <deck:hinata|selesnya> <cell:d0|d3|ship> <gi> <seed> <arm:old|new> [logdir|-] [budget_override] [ENV=VAL ...]
# Prints "<arm> <cell> gi=<gi> turns=<avg>"
set -u
ROOT=/workspaces/MagicDeckTester
BIN=${BIN:-$ROOT/logs/discard_impl/ab3/mtg_ab3_d6a760e5}
deck=$1; cell=$2; gi=$3; seed=$4; arm=$5; logdir=${6:--}; bud=${7:-}
shift 7 2>/dev/null || shift $#
case $deck in
  hinata) D=$ROOT/decks/Hinata2/Hinata2.cod; P=$ROOT/decks/Hinata2/Hinata2.profile.json;;
  selesnya) D=$ROOT/decks/SelesnyaLifegain/SelesnyaLifegain.cod; P=$ROOT/decks/SelesnyaLifegain/SelesnyaLifegain.profile.json;;
esac
args=("$D" --profile "$P" --seed "$seed" --game-index "$gi" --games 1 --threads 1)
case $cell in
  d0) args+=(--depth 0 --ignore-play-profile);;
  d3) args+=(--depth 3 --budget-ms "${bud:-10}" --ignore-play-profile);;
  ship) args+=(--budget-ms "${bud:-20}");;
esac
if [ "$logdir" != "-" ]; then mkdir -p "$logdir"; args+=(--log-dir "$logdir"); fi
envs=()
if [ "$arm" = old ]; then
  envs+=(MTG_DRAGONSTORM_ORDER_OPAQUE=0 MTG_PAYABLE_ORDER_RITUALS=0 MTG_COLOR_EXACT_SCALED=0 MTG_ATTACK_TAP_NEED_EXACT=0
         MTG_ATTACK_TAP_ABILITY_DISCOUNT=0 MTG_ROLLOUT_LAND_ACT_COLOR=0 MTG_SOLVE_VERIFIED_WIN=0 MTG_EDF_GORGE_RED_SIZING=0
         MTG_SOLVE_PENDING_AURA_EXACT=0)
fi
envs+=("$@")
out=$(cd "$ROOT" && env "${envs[@]}" nice -n 10 "$BIN" "${args[@]}" 2>&1)
t=$(printf '%s\n' "$out" | awk '/avg \(turns\)/{print $4}')
echo "$deck $cell gi=$gi seed=$seed arm=$arm extra=[$*] bud=${bud:-def} turns=$t"

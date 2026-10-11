#!/usr/bin/env bash
# rg.sh <deck> <cell> <gi> <seed> <arm:new|old|custom> <logdir|-> [extra args...]
# env passthrough: any MTG_* set in the caller environment is honoured.
# arm=old sets every FIX lever =0 (then caller env can re-enable one by exporting it AFTER via LEVER_ON).
# LEVER_ON="MTG_X MTG_Y" re-enables those on the old arm; LEVER_OFF="MTG_X" disables on new arm.
set -u
R=/workspaces/MagicDeckTester
BIN=${BIN:-$R/logs/discard_impl/ab3/mtg_ab3_d6a760e5}
deck=$1; cell=$2; gi=$3; seed=$4; arm=$5; logdir=$6; shift 6
case $deck in
  fivecolour) D=decks/FiveColour/FiveColour.cod; P=decks/FiveColour/FiveColour.profile.json;;
  stompy) D=decks/StompySurprise/StompySurprise.cod; P=decks/StompySurprise/StompySurprise.profile.json;;
  giants) D=decks/Giants/Giants.cod; P=decks/Giants/Giants.profile.json;;
  bruna) D=decks/Bruna/Bruna.cod; P=decks/Bruna/Bruna.profile.json;;
  *) echo "unknown deck"; exit 2;;
esac
LEVERS="MTG_DRAGONSTORM_ORDER_OPAQUE MTG_PAYABLE_ORDER_RITUALS MTG_COLOR_EXACT_SCALED MTG_ATTACK_TAP_NEED_EXACT MTG_ATTACK_TAP_ABILITY_DISCOUNT MTG_ROLLOUT_LAND_ACT_COLOR MTG_SOLVE_VERIFIED_WIN MTG_EDF_GORGE_RED_SIZING MTG_SOLVE_PENDING_AURA_EXACT"
ENVS=()
if [ "$arm" = old ]; then
  for l in $LEVERS; do ENVS+=("$l=0"); done
  for l in ${LEVER_ON:-}; do ENVS+=("$l=1"); done
else
  for l in ${LEVER_OFF:-}; do ENVS+=("$l=0"); done
fi
case $cell in
  d0) CA=(--depth 0 --ignore-play-profile);;
  d3) CA=(--depth 3 --budget-ms 10 --ignore-play-profile);;
  ship) CA=(--budget-ms 20);;
  ship4x) CA=(--budget-ms 80);;
  shipb0) CA=(--budget-ms 0);;
  d3b0) CA=(--depth 3 --budget-ms 0 --ignore-play-profile);;
  d5b0) CA=(--depth 5 --budget-ms 0 --ignore-play-profile);;
  *) echo "bad cell"; exit 2;;
esac
LA=()
if [ "$logdir" != - ]; then mkdir -p "$logdir"; LA=(--log-dir "$logdir"); fi
cd $R
env "${ENVS[@]}" nice -n 10 "$BIN" "$D" --profile "$P" --seed "$seed" --game-index "$gi" --games 1 "${CA[@]}" --threads 1 "${LA[@]}" "$@"

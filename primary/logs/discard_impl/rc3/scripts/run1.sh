#!/bin/bash
# Run ONE game of the ab3 kit.
# usage: run1.sh <deck: edf|snow> <cell: d0|d3|ship> <gi> <seed> <arm: new|old|custom> <outdir> [binary] -- extra env as VAR=VAL after outdir via ENV_EXTRA
# Extra env vars are passed through the environment of the caller (export before calling).
set -u
ROOT=/workspaces/MagicDeckTester
deck=$1; cell=$2; gi=$3; seed=$4; arm=$5; out=$6
BIN=${BIN:-$ROOT/logs/discard_impl/ab3/mtg_ab3_d6a760e5}
case $deck in
  edf)  D=$ROOT/decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod; P=$ROOT/decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json;;
  snow) D=$ROOT/decks/Snow/Snow.cod; P=$ROOT/decks/Snow/Snow.profile.json;;
  *) echo "bad deck"; exit 2;;
esac
case $cell in
  d0)   CA="--depth 0 --ignore-play-profile";;
  d3)   CA="--depth 3 --budget-ms 10 --ignore-play-profile";;
  ship) CA="--budget-ms 20";;
  *) echo "bad cell"; exit 2;;
esac
OLDENV=""
if [ "$arm" = old ]; then
  OLDENV="MTG_DRAGONSTORM_ORDER_OPAQUE=0 MTG_PAYABLE_ORDER_RITUALS=0 MTG_COLOR_EXACT_SCALED=0 MTG_ATTACK_TAP_NEED_EXACT=0 MTG_ATTACK_TAP_ABILITY_DISCOUNT=0 MTG_ROLLOUT_LAND_ACT_COLOR=0 MTG_SOLVE_VERIFIED_WIN=0 MTG_EDF_GORGE_RED_SIZING=0 MTG_SOLVE_PENDING_AURA_EXACT=0"
fi
mkdir -p "$out"
# shellcheck disable=SC2086
env $OLDENV ${ENV_EXTRA:-} nice -n 19 "$BIN" "$D" --profile "$P" --seed "$seed" --game-index "$gi" --games 1 $CA --threads 1 --log-dir "$out" > "$out/stdout.txt" 2> "$out/stderr.txt"
echo "exit=$?" >> "$out/stdout.txt"

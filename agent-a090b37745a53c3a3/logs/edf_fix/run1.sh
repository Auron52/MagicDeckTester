#!/bin/bash
# Run ONE EDF game. usage: run1.sh <cell: d0|d3|ship> <gi> <seed> <arm: new|old> <outdir> [binary]
# Extra env: export ENV_EXTRA="VAR=VAL ..." before calling. Binaries run from the build volume, under
# lowprio.sh (cores 0-5, nice 19); <outdir> must be on the build volume (scratch), not the 9p mount.
set -u
ROOT=/workspaces/MagicDeckTester
WT=$ROOT/.claude/worktrees/agent-a090b37745a53c3a3
cell=$1; gi=$2; seed=$3; arm=$4; out=$5
BIN=${6:-$ROOT/build/wt/agent-a090b37745a53c3a3/Release/mtg}
D=$WT/decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod
P=$WT/decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json
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
env $OLDENV ${ENV_EXTRA:-} bash $ROOT/build/scratch/lowprio.sh "$BIN" "$D" --profile "$P" --seed "$seed" --game-index "$gi" --games 1 $CA --threads 1 --log-dir "$out" > "$out/stdout.txt" 2> "$out/stderr.txt"
echo "exit=$?" >> "$out/stdout.txt"
grep -h "avg" "$out/stdout.txt" | head -2

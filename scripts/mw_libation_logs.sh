#!/usr/bin/env bash
# Collect PAIRED per-game JSON logs for the divergent games of one aliased Libation screen.
#
# The screen ran through `mtg --batch`, which has no --log-dir, so the logs have to come from
# single-game re-invocations. That is only sound if the re-invocation reproduces the batch's win
# turn exactly, and the classifier CHECKS that per game against the screen's own [win] lines rather
# than assuming it.
#
# `--seed base+gi` ALONE IS NOT ENOUGH -- it silently mis-reproduced 11% of games. The shuffle seed
# is `seed + i`, so `--seed base+gi --games 1` deals the right library, but the game index is used
# for more than the shuffle and must be passed separately as `--game-index gi`. Verified: with
# --game-index the repro matches the batch on every game tested; without it, 39 of 357 disagreed
# while being perfectly deterministic run-to-run -- i.e. it looks like a clean reproduction and is
# not one. (A 600-game single-process run agreed with the batch 600/600, which is what localised
# the difference to the per-invocation index rather than to the batch.)
#
# usage: mw_libation_logs.sh <anger|oracle> <indices-file> <outdir>
#   <indices-file>: one game index per line
set -euo pipefail
cd "$(dirname "$0")/.."

WHICH="$1"; IDX="$2"; OUT="$3"
case "$WHICH" in
  anger)  ARM=lib_over_anger;  ALIAS=alias_anger;  REPRO=repro_anger  ;;
  oracle) ARM=lib_over_oracle; ALIAS=alias_oracle; REPRO=repro_oracle ;;
  *) echo "usage: $0 <anger|oracle> <indices-file> <outdir>" >&2; exit 2 ;;
esac

D="logs/deckcmp/Mirrorwing Dragon"
SEED=2600000
PROF="$(realpath "logs/mw_libation/$REPRO/Mirrorwing Dragon.profile.json")"
VAL="$(realpath "logs/mw_libation/$ALIAS/Mirrorwing Dragon.value.json")"
PIN="$(realpath "$D/base/Mirrorwing Dragon.txt")"

mkdir -p "$OUT/base" "$OUT/$ARM"

run_one() {
  local arm="$1" gi="$2"
  MTG_DECK_NUMBERING="$D/$arm/numbering.json" \
  MTG_VALUE_PROFILE="$VAL" MTG_VALUE_MODEL=0 MTG_LADDER_VALUE_LEAF=1 \
  MTG_PROVIDER_DECK="$PIN" \
  build/Release/mtg "$D/$arm/Mirrorwing Dragon.txt" \
    --games 1 --seed $((SEED + gi)) --game-index "$gi" --max-turns 8 --threads 1 \
    --profile "$PROF" --log-dir "$OUT/$arm" >/dev/null 2>&1
}
export -f run_one
export D PROF VAL PIN SEED OUT

{ while read -r gi; do [ -n "$gi" ] || continue; echo "base $gi"; echo "$ARM $gi"; done < "$IDX"; } \
  | xargs -P "$(nproc)" -n 2 bash -c 'run_one "$0" "$1"'

echo "collected: base=$(ls "$OUT/base" | wc -l)  $ARM=$(ls "$OUT/$ARM" | wc -l)"

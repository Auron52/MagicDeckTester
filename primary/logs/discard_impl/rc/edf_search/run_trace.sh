#!/bin/bash
# Usage: run_trace.sh <cell> <turn> <tag> [extra env assignments...]
# Re-runs both arms of a cell with the root-decision trace for <turn> (print-only diagnostics).
D=/workspaces/MagicDeckTester/logs/discard_impl/rc/edf_search
B=/workspaces/MagicDeckTester/build/PGO/mtg
DECK=decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod
PROF=decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json
cell=$1; turn=$2; tag=$3; shift 3
cd /workspaces/MagicDeckTester || exit 1
case $cell in
  g63)  args=(--game-index 63 --depth 3 --budget-ms 10 --ignore-play-profile) ;;
  g163) args=(--game-index 163 --depth 5 --budget-ms 20) ;;
  g14)  args=(--game-index 14 --depth 5 --budget-ms 20) ;;
esac
seed=217164; [ "$cell" = g14 ] && seed=217015
for flag in 0 1; do
  arm=off; [ $flag = 1 ] && arm=on
  env "$@" MTG_EDF_BUCKET_DISCARD=$flag MTG_TRACE_SOLVE=1 MTG_TRACE_SOLVE_TURN=$turn nice -n 19 "$B" "$DECK" --profile "$PROF" \
     --games 1 --seed $seed "${args[@]}" --threads 1 > "$D/${cell}_${arm}.$tag.out" 2> "$D/${cell}_${arm}.$tag.err" &
done
wait
echo done

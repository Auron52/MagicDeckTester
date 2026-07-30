#!/usr/bin/env bash
# DETACHED A/B validation chain for the mm6 staged profiles (TH R=41, Slivers R=60, Knights R=60).
# KEEP A/Bs first (exhaustive keep vs static -- the value-leaf-on-mulligan safety test), then
# confounded BOTTOM A/Bs. Points each run at the STAGED logs/<stem>_gen/pooled.profile.json (NOT adopted).
# Overrides KM_STATIC/KM_EXH_PROFILE to folder+staged paths (harness has flat pre-folder-move defaults).
# Run: setsid nohup bash test/mm6_ab_chain.sh </dev/null &>logs/mm6_ab_chain.out &
set -uo pipefail
cd "$(dirname "$0")/.."
LOG=logs/mm6_ab_chain.out
say(){ echo "[$(date -u +%FT%TZ)] $*" | tee -a "$LOG"; }
declare -A DECKF=(
  [treasure_hunt]=decks/treasure_hunt/treasure_hunt.txt
  [slivers_vial]=decks/slivers_vial/slivers_vial.txt
  [Knights]=decks/Knights/Knights.cod
)
say "AB CHAIN start (HEAD=$(git rev-parse --short HEAD), PID=$$)"
for mode in keep bottom; do
  for stem in treasure_hunt slivers_vial Knights; do
    say "########## $stem $mode A/B START ##########"
    KM_DECK="${DECKF[$stem]}" \
    KM_STATIC="decks/$stem/$stem.profile.json" \
    KM_EXH_PROFILE="logs/${stem}_gen/pooled.profile.json" \
    KM_MODE="$mode" bash test/keepmodel_exhaustive_ab.sh
    say "########## $stem $mode A/B DONE (exit $?) ##########"
  done
done
say "AB CHAIN COMPLETE"

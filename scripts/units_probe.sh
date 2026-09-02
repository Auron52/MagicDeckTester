#!/usr/bin/env bash
# PER-DECK, PER-ARM VIRTUAL-COST probe: units_total and committed depth for a lever, at each
# deck's own shipped play settings.
#
# WHY ONE PROCESS PER CELL (and not the pooled --batch this repo otherwise mandates): the
# rollout-stats counters are PROCESS-GLOBAL atomics dumped once at exit (see TurnSolver.cpp
# "[rollout-stats] units_total="). A pooled batch running both arms in one process therefore
# reports their SUM, which is not a ratio and cannot answer "what does this lever cost". So the
# cells must be separate processes. They are run CONCURRENTLY (xargs -P), so the box still stays
# saturated behind a single global tail -- this is not the serial per-item loop the pooling rule
# forbids, it is the same pool with the aggregation boundary moved into the process.
#
# WHY A SMALL SAMPLE IS ENOUGH: units are charged at ConsumeAt() sites and are fully deterministic
# given (seed, flags, deck) -- there is no sampling noise in the counter itself, only deck-level
# variation in which games get played. A few hundred games pins the ratio to well under the effect
# size. For the SAME reason this probe is CONTENTION-PROOF: a virtual counter does not care what
# else is running on the host. Wall time does -- for that use scripts/wall_probe.sh on a QUIET box.
#
# THE NUMBER THAT MATTERS IS units_total AT MATCHED budget_ms. budget_ms is an allowance in units,
# not milliseconds, so a lever that frees work does not bank it -- the search REINVESTS it and the
# unit count barely moves while depth rises. Read units_total together with id_depth/committed_depth:
# more units at equal depth is pure cost; equal units at greater depth is the lever paying for itself.
set -u

BIN=./build/Release/mtg
OUT=${OUT:-logs/units_probe}
GAMES=${GAMES:-300}
SEED=${SEED:-5500001}
JOBS=${JOBS:-24}
mkdir -p "$OUT"

# arm -> env assignments. Empty = shipped defaults.
RECIPE="MTG_BP_SITE3=1 MTG_BP_SITE3_DEFER=1 MTG_BP_NO_GREEDY_CONT=1 MTG_BP_NODE=1 MTG_BP_NODE_ROOTTURN=1"

# deck key -> "decklist|profile". Mirrors test/regression_cases.sh's DECK_FILE/DECK_PROF.
DECKS=(
 "slivers|decks/slivers_vial/slivers_vial.txt|decks/slivers_vial/slivers_vial.profile.json"
 "burn|decks/burn/burn.txt|decks/burn/burn.profile.json"
 "th|decks/treasure_hunt/treasure_hunt.txt|decks/treasure_hunt/treasure_hunt.profile.json"
 "knights|decks/Knights/Knights.cod|decks/Knights/Knights.profile.json"
 "antilife|decks/Anti-Lifegain/Anti-Lifegain.cod|decks/Anti-Lifegain/Anti-Lifegain.profile.json"
 "hinata|decks/Hinata2/Hinata2.cod|decks/Hinata2/Hinata2.profile.json"
 "dragonstorm|decks/Dragonstorm/Dragonstorm.cod|decks/Dragonstorm/Dragonstorm.profile.json"
 "auras|decks/Auras/Auras.cod|decks/Auras/Auras.profile.json"
 "goblins|decks/Goblins/Goblins.cod|decks/Goblins/Goblins.profile.json"
 "creature_giving|decks/Creature Giving/Creature Giving.cod|decks/Creature Giving/Creature Giving.profile.json"
 "mirrorwing|decks/Mirrorwing Dragon/Mirrorwing Dragon.cod|decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json"
 "fivecolour|decks/FiveColour/FiveColour.cod|decks/FiveColour/FiveColour.profile.json"
 "stompy|decks/StompySurprise/StompySurprise.cod|decks/StompySurprise/StompySurprise.profile.json"
 "minotaur|decks/Minotaur/Minotaur.cod|decks/Minotaur/Minotaur.profile.json"
 "kitty|decks/KittyEquipment/KittyEquipment.cod|decks/KittyEquipment/KittyEquipment.profile.json"
 "dragons|decks/Dragons/Dragons.cod|decks/Dragons/Dragons.profile.json"
)

# Emit one command per (deck, arm). depth/budget are OMITTED on purpose so the engine resolves each
# deck's own value_play lock -- the same "at play settings" rule the quality gate used.
gen_cells() {
  for entry in "${DECKS[@]}"; do
    IFS='|' read -r key deck prof <<< "$entry"
    for arm in base roott; do
      printf '%s\t%s\t%s\t%s\n' "$key" "$arm" "$deck" "$prof"
    done
  done
}

run_cell() {
  IFS=$'\t' read -r key arm deck prof <<< "$1"
  local envs=""
  [ "$arm" = roott ] && envs="$RECIPE"
  # shellcheck disable=SC2086
  env $envs MTG_ROLLOUT_STATS=1 "$BIN" "$deck" --profile "$prof" \
      --games "$GAMES" --seed "$SEED" --threads 1 \
      > "$OUT/$key.$arm.out" 2> "$OUT/$key.$arm.err"
  echo "[units] done $key/$arm"
}
export -f run_cell
export BIN OUT GAMES SEED RECIPE

if [ "${1:-}" = "--collect" ]; then
  printf '%-16s %14s %14s %8s %10s %10s %8s\n' deck base_units roott_units ratio base_idd roott_idd avg_d
  for entry in "${DECKS[@]}"; do
    IFS='|' read -r key _ _ <<< "$entry"
    b=$(grep -o 'units_total=[0-9]*' "$OUT/$key.base.err" 2>/dev/null | tail -1 | cut -d= -f2)
    r=$(grep -o 'units_total=[0-9]*' "$OUT/$key.roott.err" 2>/dev/null | tail -1 | cut -d= -f2)
    bd=$(grep -o 'id_depth n=[0-9]* mean=[0-9.]*' "$OUT/$key.base.err" 2>/dev/null | tail -1 | sed 's/.*mean=//')
    rd=$(grep -o 'id_depth n=[0-9]* mean=[0-9.]*' "$OUT/$key.roott.err" 2>/dev/null | tail -1 | sed 's/.*mean=//')
    ba=$(grep -o 'avg (turns)   : [0-9.]*' "$OUT/$key.base.out" 2>/dev/null | awk '{print $4}')
    ra=$(grep -o 'avg (turns)   : [0-9.]*' "$OUT/$key.roott.out" 2>/dev/null | awk '{print $4}')
    [ -z "${b:-}" ] && b=0; [ -z "${r:-}" ] && r=0
    ratio=$(awk -v b="$b" -v r="$r" 'BEGIN{ if (b>0) printf "%.4f", r/b; else printf "n/a" }')
    printf '%-16s %14s %14s %8s %10s %10s %8s\n' \
      "$key" "$b" "$r" "$ratio" "${bd:-n/a}" "${rd:-n/a}" "${ba:-n/a}/${ra:-n/a}"
  done
  exit 0
fi

gen_cells | xargs -d '\n' -P "$JOBS" -I{} bash -c 'run_cell "$@"' _ {}
echo "[units] ALL DONE -> $OUT   (read with: bash scripts/units_probe.sh --collect)"

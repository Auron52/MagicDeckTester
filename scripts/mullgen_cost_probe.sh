#!/usr/bin/env bash
# Deterministic COST table for mulligan-generation settings.
#
# Cost is measured in WORK UNITS, not seconds. Wall-clock cannot price a generation setting: it moves
# with whatever else is on the box and differs per machine, so a setting derived from seconds is not
# reproducible and cannot survive the cross-machine profile handoff. Work units are a pure function of
# (deck, seed, depth, budget), so a handful of rollouts pins the number exactly -- which is why this
# probe runs at R=4 and still produces an exact answer.
set -u -o pipefail

OUT=logs/mullgen_iso
BIN=./build/Profile/mtg-analyze
CARDS=src/cards/data/cards.json
R=${R:-4}

DECKS=(
  "decks/burn/burn.txt|burn|6|20"
  "decks/slivers_vial/slivers_vial.txt|slivers|5|20"
  "decks/Knights/Knights.cod|knights|5|20"
  "decks/treasure_hunt/treasure_hunt.txt|treasure_hunt|5|20"
  "decks/Dragonstorm/Dragonstorm.cod|dragonstorm|5|20"
  "decks/Auras/Auras.cod|auras|5|20"
  "decks/Anti-Lifegain/Anti-Lifegain.cod|antilife|5|20"
  "decks/Hinata2/Hinata2.cod|hinata|5|20"
  "decks/Goblins/Goblins.cod|goblins|6|40"
)
ARMS=("d0b3|0|3" "d1b3|1|3" "d2b3|2|3" "d3b3|3|3" "d5b20|5|20")

: > "$OUT/cost_units.tsv"
for entry in "${DECKS[@]}"; do
  IFS='|' read -r deck label rd rb <<<"$entry"
  comps="$OUT/$label.comps"
  [[ -f "$comps" ]] || { echo "skip $label (no comps yet)"; continue; }
  for a in "${ARMS[@]}" "ref|$rd|$rb"; do
    IFS='|' read -r arm d b <<<"$a"
    line=$(MTG_SCORE_COMPS=1 MTG_SCORE_FILE="$comps" MTG_SCORE_R="$R" \
      MTG_EQUIV_DEPTH="$d" MTG_SCORE_BUDGET_MS="$b" \
      "$BIN" "$deck" --cards-json "$CARDS" 2>&1 >/dev/null | grep '^\[score\]')
    upr=$(sed -n 's/.*units_per_rollout=\([0-9.]*\).*/\1/p' <<<"$line")
    printf '%s\t%s\t%s\t%s\t%s\n' "$label" "$arm" "$d" "$b" "${upr:-NA}" >> "$OUT/cost_units.tsv"
    printf '  %-14s %-7s d%-2s b%-3s  units/rollout=%s\n' "$label" "$arm" "$d" "$b" "${upr:-NA}"
  done
done
echo "COST_PROBE_DONE"

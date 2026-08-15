#!/usr/bin/env bash
# Iso-cost mulligan-gen depth sweep.
#
# Scores the SAME committed comps at several generation depths plus the deck's SHIPPED PLAY
# settings (the reference), recording both the labels and the wall-clock. Downstream
# (mullgen_isocost.py) turns that into the only comparison that decides a gen setting:
#
#     error(arm) = centered_bias(arm)^2 + per_rollout_var(arm) / R(arm),  R(arm) = budget / cost(arm)
#
# i.e. a cheaper arm is allowed to spend its saving on MORE ROLLOUTS. Straight time savings is not
# the question -- "slightly higher R at d2 vs lower R at d3 for the same cost" is (user, 2026-08-15).
#
# Runs ONE arm at a time on the full box: the scorer parallelises over comps internally (128 comps =
# 4 exact waves on 32 threads), and overlapping invocations would corrupt the wall-clock that the
# cost half of the model is read from.
set -u -o pipefail

OUT=logs/mullgen_iso_v2
BIN=./build/Release/mtg-analyze
CARDS=src/cards/data/cards.json
N=${N:-128}
R=${R:-60}
SEED=${SEED:-20260815}

mkdir -p "$OUT"

# deck_path|label|ref_depth|ref_budget   (ref = value_play target_depth/budget_ms, the policy the
# deck actually plays -- labels exist to rank hands FOR that policy, so it is the right reference.)
# Ordered CHEAPEST-FIRST so usable numbers land early instead of all at the end.
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

# arm label|depth|budget_ms  ("ref" is substituted per deck).
#
# Two arms exist for reasons that are easy to miss:
#   d5b3  -- trust DEPTH but a starved BUDGET. Reaching the value leaf needs depth >= trust AND enough
#            budget to COMPLETE that depth; starve it and the search stops early and plays out
#            instead, so the cheap-budget arm can cost MORE (slivers: d5b3 1602 > d5b20 1318).
#   d3b20 -- the converse: real budget, depth below trust. Separates "budget bound" from "depth bound".
ARMS=("d0b3|0|3" "d1b3|1|3" "d2b3|2|3" "d3b3|3|3" "d5b3|5|3" "d3b20|3|20" "d5b20|5|20")

echo "=== iso-cost gen-depth sweep  N=$N R=$R seed=$SEED  $(date -Is) ==="

for entry in "${DECKS[@]}"; do
  IFS='|' read -r deck label rd rb <<<"$entry"
  if [[ ! -f "$deck" ]]; then
    for alt in "${deck%.txt}.cod"; do [[ -f "$alt" ]] && deck="$alt"; done
  fi
  [[ -f "$deck" ]] || { echo "SKIP $label -- no decklist at $deck"; continue; }

  comps="$OUT/$label.comps"
  python3 scripts/mullgen_comps.py "$deck" -n "$N" --seed "$SEED" -o "$comps" \
      2> "$OUT/$label.comps.log" || { echo "SKIP $label -- comp sampling failed"; continue; }
  echo "--- $label  ($(head -c 80 "$OUT/$label.comps.log" | tr -d '\n'))"

  # the deck's shipped play settings, as an extra arm (the reference)
  arms=("${ARMS[@]}" "ref|$rd|$rb")
  seen_ref=""
  for a in "${arms[@]}"; do
    IFS='|' read -r arm d b <<<"$a"
    # skip a fixed arm that coincides with this deck's reference (scored once, aliased)
    if [[ "$arm" != "ref" && "$d" == "$rd" && "$b" == "$rb" ]]; then seen_ref="$arm"; fi
    if [[ "$arm" == "ref" && -n "$seen_ref" ]]; then
      cp "$OUT/$label.$seen_ref.tsv" "$OUT/$label.ref.tsv" 2>/dev/null
      grep -h "^$label	$seen_ref	" "$OUT/timing.tsv" 2>/dev/null \
        | sed "s/	$seen_ref	/	ref	/" >> "$OUT/timing.tsv"
      echo "    ref = $seen_ref (aliased, not re-scored)"
      continue
    fi
    t0=$(date +%s.%N)
    MTG_SCORE_COMPS=1 MTG_SCORE_FILE="$comps" MTG_SCORE_R="$R" \
      MTG_EQUIV_DEPTH="$d" MTG_SCORE_BUDGET_MS="$b" \
      "$BIN" "$deck" --cards-json "$CARDS" > "$OUT/$label.$arm.tsv" 2> "$OUT/$label.$arm.err"
    rc=$?
    t1=$(date +%s.%N)
    wall=$(echo "$t1 - $t0" | bc)
    rows=$(grep -c $'\t' "$OUT/$label.$arm.tsv" 2>/dev/null || echo 0)
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$label" "$arm" "$d" "$b" "$wall" "$rows" "$rc" \
      >> "$OUT/timing.tsv"
    printf '    %-6s d%-2s b%-3s  wall=%8.1fs  rows=%-4s rc=%s\n' "$arm" "$d" "$b" "$wall" "$rows" "$rc"
  done
done

echo "=== done $(date -Is) ==="

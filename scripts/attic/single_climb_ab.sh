#!/usr/bin/env bash
# R-hint + live-climb A/B: shipped LADDER vs the frozen-R single pass WITH the up-climb. The R hint picks the
# start depth cheaply; the climb corrects it upward from this decision's live measured cost (+ probe leaf ratio)
# when budget allows, capped at CAP. Deterministic + adaptive. Tests whether a conservative global R + climb is
# quality-neutral across decks (killing per-deck R fragility).
#   usage: GAMES=300 SEED=12012 CAP=5 R=120 scripts/single_climb_ab.sh
set -uo pipefail
BIN=build/Release/mtg
declare -A F=( [antilife]=decks/Anti-Lifegain/Anti-Lifegain.cod [hinata]=decks/Hinata2/Hinata2.cod \
  [th]=decks/treasure_hunt/treasure_hunt.txt [slivers]=decks/slivers_vial/slivers_vial.txt \
  [knights]=decks/Knights/Knights.cod [burn]=decks/burn/burn.txt )
declare -A P=( [antilife]=decks/Anti-Lifegain/Anti-Lifegain.profile.json [hinata]=decks/Hinata2/Hinata2.profile.json \
  [th]=decks/treasure_hunt/treasure_hunt.profile.json [slivers]=decks/slivers_vial/slivers_vial.profile.json \
  [knights]=decks/Knights/Knights.profile.json [burn]=decks/burn/burn.profile.json )
G=${GAMES:-300}; S=${SEED:-12012}; CAP=${CAP:-5}; R=${R:-120}
DECKS=${DECKS:-"antilife hinata th slivers knights burn"}
run() { local out; out=$(env $2 MTG_ROLLOUT_STATS=1 MTG_HYBRID_STATS=1 "$BIN" "${F[$1]}" --profile "${P[$1]}" \
    --seed "$S" --games "$G" --max-turns 8 --threads 0 2>&1)
  local hh; hh=$(echo "$out"|grep -oP '^\s+h[0-9]+: [0-9]+'|tr -s ' '|paste -sd' ' -)
  echo "$(echo "$out"|grep -oP 'avg \(turns\)\s*:\s*\K[0-9.]+') $(echo "$out"|grep -oP 'turn_steps=\K[0-9]+'|head -1) | $hh"; }
SINGLE="MTG_ESC_SINGLE=1 MTG_ESC_SINGLE_PREDICT=1 MTG_ESC_SINGLE_ABS=$CAP MTG_ESC_SINGLE_FALLBACK=1 MTG_ESC_SINGLE_R=$R MTG_ESC_SINGLE_CLIMB=1"
echo "== climb A/B: R=$R cap=$CAP  ${G}g seed $S =="
for deck in $DECKS; do
  IFS='|' read lmain lhh < <(run "$deck" ""); read lLP lTS <<< "$lmain"
  IFS='|' read smain shh < <(run "$deck" "$SINGLE"); read sLP sTS <<< "$smain"
  pct=$(awk -v s="$sTS" -v l="$lTS" 'BEGIN{if(l>0)printf "%.0f",100*s/l;else print"NA"}')
  dlp=$(awk -v s="$sLP" -v l="$lLP" 'BEGIN{printf "%+.4f",s-l}')
  printf "%-9s ladder LP=%-8s TS=%-10s | climb LP=%-8s TS=%-10s | dLP=%s work=%s%%\n" \
    "$deck" "$lLP" "$lTS" "$sLP" "$sTS" "$dlp" "$pct"
  printf "            ladder h:%s\n            climb  h:%s\n" "$lhh" "$shh"
done
echo DONE

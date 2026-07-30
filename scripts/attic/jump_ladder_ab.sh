#!/usr/bin/env bash
# ADAPTIVE jump-ladder A/B: shipped LADDER vs MTG_ESC_JUMP (run d1,d2 -> measure live growth -> jump to the
# deepest AFFORDABLE depth, skipping the middle rungs; overrun guard adapts down). Fully per-decision (no frozen
# R, no thread_local) => deterministic + adaptive. Optional MTG_ESC_DEPTH_CAP=<cap> trims past-convergence waste.
#   usage: GAMES=300 SEED=12012 CAP=0 scripts/jump_ladder_ab.sh   (CAP=0 => no cap)
set -uo pipefail
BIN=build/Release/mtg
declare -A F=( [antilife]=decks/Anti-Lifegain/Anti-Lifegain.cod [hinata]=decks/Hinata2/Hinata2.cod \
  [th]=decks/treasure_hunt/treasure_hunt.txt [slivers]=decks/slivers_vial/slivers_vial.txt \
  [knights]=decks/Knights/Knights.cod [burn]=decks/burn/burn.txt )
declare -A P=( [antilife]=decks/Anti-Lifegain/Anti-Lifegain.profile.json [hinata]=decks/Hinata2/Hinata2.profile.json \
  [th]=decks/treasure_hunt/treasure_hunt.profile.json [slivers]=decks/slivers_vial/slivers_vial.profile.json \
  [knights]=decks/Knights/Knights.profile.json [burn]=decks/burn/burn.profile.json )
G=${GAMES:-300}; S=${SEED:-12012}; CAP=${CAP:-0}
DECKS=${DECKS:-"antilife hinata th slivers knights burn"}
capenv=""; [ "$CAP" -gt 0 ] && capenv="MTG_ESC_DEPTH_CAP=$CAP"
run() { local out; out=$(env $2 MTG_ROLLOUT_STATS=1 MTG_HYBRID_STATS=1 "$BIN" "${F[$1]}" --profile "${P[$1]}" \
    --seed "$S" --games "$G" --max-turns 8 --threads 0 2>&1)
  local hh; hh=$(echo "$out"|grep -oP '^\s+h[0-9]+: [0-9]+'|tr -s ' '|paste -sd' ' -)
  echo "$(echo "$out"|grep -oP 'avg \(turns\)\s*:\s*\K[0-9.]+') $(echo "$out"|grep -oP 'turn_steps=\K[0-9]+'|head -1) | $hh"; }
for deck in $DECKS; do
  IFS='|' read lmain lhh < <(run "$deck" ""); read lLP lTS <<< "$lmain"
  IFS='|' read jmain jhh < <(run "$deck" "MTG_ESC_JUMP=1 $capenv"); read jLP jTS <<< "$jmain"
  pct=$(awk -v s="$jTS" -v l="$lTS" 'BEGIN{if(l>0)printf "%.0f",100*s/l;else print"NA"}')
  dlp=$(awk -v s="$jLP" -v l="$lLP" 'BEGIN{printf "%+.4f",s-l}')
  printf "%-9s ladder LP=%-8s TS=%-10s |jump(cap%s) LP=%-8s TS=%-10s | dLP=%s work=%s%%\n" \
    "$deck" "$lLP" "$lTS" "$CAP" "$jLP" "$jTS" "$dlp" "$pct"
  printf "            ladder h:%s\n            jump   h:%s\n" "$lhh" "$jhh"
done
echo DONE

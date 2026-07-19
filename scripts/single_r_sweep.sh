#!/usr/bin/env bash
# Deterministic PREDICT-path R sweep: for each deck, ladder baseline vs predicted-affordable single pass at a
# FIXED R (MTG_ESC_SINGLE_R) across cap. Frozen R => deterministic. Finds the per-deck R that is quality-neutral
# (dLP~0) without the light-deck fallback explosion. Reports LP + turn_steps + dLP + work%.
#   usage: GAMES=300 SEED=12012 CAP=3 RS="15 40 120" scripts/single_r_sweep.sh
set -uo pipefail
BIN=build/Release/mtg
declare -A F=( [antilife]=decks/Anti-Lifegain/Anti-Lifegain.cod [hinata]=decks/Hinata2/Hinata2.cod \
  [th]=decks/treasure_hunt/treasure_hunt.txt [slivers]=decks/slivers_vial/slivers_vial.txt \
  [knights]=decks/Knights/Knights.cod [burn]=decks/burn/burn.txt )
declare -A P=( [antilife]=decks/Anti-Lifegain/Anti-Lifegain.profile.json [hinata]=decks/Hinata2/Hinata2.profile.json \
  [th]=decks/treasure_hunt/treasure_hunt.profile.json [slivers]=decks/slivers_vial/slivers_vial.profile.json \
  [knights]=decks/Knights/Knights.profile.json [burn]=decks/burn/burn.profile.json )
G=${GAMES:-300}; S=${SEED:-12012}; CAP=${CAP:-3}; RS=${RS:-"15 40 120"}
DECKS=${DECKS:-"antilife hinata th slivers knights burn"}
run() { local out; out=$(env $2 MTG_ROLLOUT_STATS=1 "$BIN" "${F[$1]}" --profile "${P[$1]}" \
    --seed "$S" --games "$G" --max-turns 8 --threads 0 2>&1)
  echo "$(echo "$out"|grep -oP 'avg \(turns\)\s*:\s*\K[0-9.]+') $(echo "$out"|grep -oP 'turn_steps=\K[0-9]+'|head -1)"; }
for deck in $DECKS; do
  read lLP lTS < <(run "$deck" "")
  printf "%-9s ladder   LP=%-8s TS=%-10s\n" "$deck" "$lLP" "$lTS"
  for R in $RS; do
    read sLP sTS < <(run "$deck" "MTG_ESC_SINGLE=1 MTG_ESC_SINGLE_PREDICT=1 MTG_ESC_SINGLE_ABS=$CAP MTG_ESC_SINGLE_FALLBACK=1 MTG_ESC_SINGLE_R=$R")
    pct=$(awk -v s="$sTS" -v l="$lTS" 'BEGIN{if(l>0)printf "%.0f",100*s/l;else print"NA"}')
    dlp=$(awk -v s="$sLP" -v l="$lLP" 'BEGIN{printf "%+.4f",s-l}')
    printf "%-9s R=%-4s cap%d LP=%-8s TS=%-10s dLP=%s work=%s%%\n" "$deck" "$R" "$CAP" "$sLP" "$sTS" "$dlp" "$pct"
  done
done
echo DONE

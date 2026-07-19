#!/usr/bin/env bash
# Beam-aware climb A/B (quality AND performance). Requires the value.json to NOT have escalation_cap set
# (so the bare run == LADDER baseline); the single-depth arms are driven via the env research path so we can
# vary MTG_ESC_CLIMB_GROWTH (0 = shipped leaf-ratio, 1 = measured-growth steps>=2, 2 = cost-ratio bootstrap).
# Reports, per arm: LP (avg turns), turn_steps (deterministic work), dLP vs ladder, work vs ladder, work vs mode0.
#   usage: GAMES=300 SEED=12012 DECKS="hinata antilife slivers" MODES="0 1 2" scripts/beam_growth_ab.sh
set -uo pipefail
BIN=build/Release/mtg
declare -A F=( [antilife]=decks/Anti-Lifegain/Anti-Lifegain.cod [hinata]=decks/Hinata2/Hinata2.cod \
  [th]=decks/treasure_hunt/treasure_hunt.txt [slivers]=decks/slivers_vial/slivers_vial.txt \
  [knights]=decks/Knights/Knights.cod [burn]=decks/burn/burn.txt )
declare -A P=( [antilife]=decks/Anti-Lifegain/Anti-Lifegain.profile.json [hinata]=decks/Hinata2/Hinata2.profile.json \
  [th]=decks/treasure_hunt/treasure_hunt.profile.json [slivers]=decks/slivers_vial/slivers_vial.profile.json \
  [knights]=decks/Knights/Knights.profile.json [burn]=decks/burn/burn.profile.json )
declare -A CAP=( [antilife]=5 [hinata]=5 [th]=5 [slivers]=5 [knights]=5 [burn]=6 )
G=${GAMES:-300}; S=${SEED:-12012}; DECKS=${DECKS:-"hinata antilife slivers"}; MODES=${MODES:-"0 1 2"}
run() { local out; out=$(env $2 MTG_ROLLOUT_STATS=1 "$BIN" "${F[$1]}" --profile "${P[$1]}" \
    --seed "$S" --games "$G" --max-turns 8 --threads 0 2>&1)
  echo "$(echo "$out"|grep -oP 'avg \(turns\)\s*:\s*\K[0-9.]+') $(echo "$out"|grep -oP 'turn_steps=\K[0-9]+'|head -1)"; }
for deck in $DECKS; do
  read lLP lTS < <(run "$deck" "")
  printf "%-9s LADDER   LP=%-8s TS=%-10s\n" "$deck" "$lLP" "$lTS"
  m0TS=0
  for m in $MODES; do
    E="MTG_ESC_SINGLE=1 MTG_ESC_SINGLE_PREDICT=1 MTG_ESC_SINGLE_ABS=${CAP[$deck]} MTG_ESC_SINGLE_FALLBACK=1 MTG_ESC_SINGLE_R=120 MTG_ESC_SINGLE_CLIMB=1 MTG_ESC_CLIMB_GROWTH=$m"
    read sLP sTS < <(run "$deck" "$E")
    [ "$m" = "0" ] && m0TS=$sTS
    dlp=$(awk -v s="$sLP" -v l="$lLP" 'BEGIN{printf "%+.4f",s-l}')
    wl=$(awk -v s="$sTS" -v l="$lTS" 'BEGIN{if(l>0)printf "%.0f",100*s/l;else print"NA"}')
    wm0=$(awk -v s="$sTS" -v m="$m0TS" 'BEGIN{if(m>0)printf "%.0f",100*s/m;else print"-"}')
    printf "%-9s mode%s    LP=%-8s TS=%-10s dLP(vs ladder)=%s  work vs ladder=%s%%  vs mode0=%s%%\n" \
      "$deck" "$m" "$sLP" "$sTS" "$dlp" "$wl" "$wm0"
  done
done
echo DONE

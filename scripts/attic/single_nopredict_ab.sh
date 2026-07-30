#!/usr/bin/env bash
# Deterministic no-predict single-depth A/B: ONE heuristic escalation pass at a FIXED cap with budget FALLBACK
# (MTG_ESC_SINGLE + _ABS=cap + _FALLBACK, NO _PREDICT). This path never touches the adaptive thread_local
# g_esc_R, so it is deterministic by construction. Compares vs the shipped LADDER. Reports LP + turn_steps.
#   usage: GAMES=300 SEED=12012 CAP=3 scripts/single_nopredict_ab.sh
set -uo pipefail
BIN=build/Release/mtg
declare -A F=( [antilife]=decks/Anti-Lifegain/Anti-Lifegain.cod [hinata]=decks/Hinata2/Hinata2.cod \
  [th]=decks/treasure_hunt/treasure_hunt.txt [slivers]=decks/slivers_vial/slivers_vial.txt \
  [knights]=decks/Knights/Knights.cod [burn]=decks/burn/burn.txt )
declare -A P=( [antilife]=decks/Anti-Lifegain/Anti-Lifegain.profile.json [hinata]=decks/Hinata2/Hinata2.profile.json \
  [th]=decks/treasure_hunt/treasure_hunt.profile.json [slivers]=decks/slivers_vial/slivers_vial.profile.json \
  [knights]=decks/Knights/Knights.profile.json [burn]=decks/burn/burn.profile.json )
G=${GAMES:-300}; S=${SEED:-12012}; CAP=${CAP:-3}
run() { local out; out=$(env $2 MTG_ROLLOUT_STATS=1 "$BIN" "${F[$1]}" --profile "${P[$1]}" \
    --seed "$S" --games "$G" --max-turns 8 --threads 0 2>&1)
  echo "$(echo "$out"|grep -oP 'avg \(turns\)\s*:\s*\K[0-9.]+') $(echo "$out"|grep -oP 'turn_steps=\K[0-9]+'|head -1)"; }
for deck in antilife hinata th slivers knights burn; do
  read lLP lTS < <(run "$deck" "")
  read sLP sTS < <(run "$deck" "MTG_ESC_SINGLE=1 MTG_ESC_SINGLE_ABS=$CAP MTG_ESC_SINGLE_FALLBACK=1")
  pct=$(awk -v s="$sTS" -v l="$lTS" 'BEGIN{if(l>0)printf "%.0f",100*s/l;else print"NA"}')
  dlp=$(awk -v s="$sLP" -v l="$lLP" 'BEGIN{printf "%+.4f",s-l}')
  printf "%-9s ladder LP=%-8s TS=%-10s | single(nopredict cap%d) LP=%-8s TS=%-10s | dLP=%s work=%s%%\n" \
    "$deck" "$lLP" "$lTS" "$CAP" "$sLP" "$sTS" "$dlp" "$pct"
done
echo DONE

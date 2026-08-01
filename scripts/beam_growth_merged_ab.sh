#!/usr/bin/env bash
# Beam-aware climb A/B on the MERGED base (post-Dragonstorm). Three arms per (deck,seed):
#   ladder = /tmp/mtg_origin (origin binary, no single-depth -> true 1..depth ladder)
#   mode0  = combined build, per-deck path (value.json escalation_cap), MTG_ESC_CLIMB_GROWTH=0 (SHIPPED)
#   mode1  = combined build, per-deck path,                            MTG_ESC_CLIMB_GROWTH=1 (beam-aware)
# Metric: avg(turns) is loss-penalized (unwon=max_turns+1). Work = deterministic turn_steps.
# Reports dLP vs ladder for each mode (SMALLER |dLP| = more neutral = beam-aware goal) + work mode1/mode0.
#   usage: GAMES=200 DECKS="hinata burn" SEEDS="2002 3003 4004 5005 6006 7007" scripts/beam_growth_merged_ab.sh
set -uo pipefail
COMB=build/Release/mtg; LAD=/tmp/mtg_origin
declare -A F=( [antilife]=decks/Anti-Lifegain/Anti-Lifegain.cod [hinata]=decks/Hinata2/Hinata2.cod \
  [th]=decks/treasure_hunt/treasure_hunt.txt [slivers]=decks/slivers_vial/slivers_vial.txt \
  [knights]=decks/Knights/Knights.cod [burn]=decks/burn/burn.txt )
declare -A P=( [antilife]=decks/Anti-Lifegain/Anti-Lifegain.profile.json [hinata]=decks/Hinata2/Hinata2.profile.json \
  [th]=decks/treasure_hunt/treasure_hunt.profile.json [slivers]=decks/slivers_vial/slivers_vial.profile.json \
  [knights]=decks/Knights/Knights.profile.json [burn]=decks/burn/burn.profile.json )
G=${GAMES:-200}; DECKS=${DECKS:-"hinata burn"}; SEEDS=${SEEDS:-"2002 3003 4004 5005 6006 7007"}; DEPTH=${DEPTH:-5}; BUD=${BUD:-20}
run() { # $1 bin  $2 deck  $3 seed  $4 env
  # NB: omit --depth -- value_play drives depth (hinata=5, burn=6); passing --depth conflicts with the profile lock.
  local out; out=$(env $4 MTG_ROLLOUT_STATS=1 "$1" "${F[$2]}" --profile "${P[$2]}" \
    --seed "$3" --games "$G" --budget-ms "$BUD" --max-turns 8 --lookahead-bottoming --threads 0 2>&1)
  echo "$(echo "$out"|grep -oP 'avg \(turns\)\s*:\s*\K[0-9.]+') $(echo "$out"|grep -oP 'turn_steps=\K[0-9]+'|head -1)"; }
printf "%-9s %-6s | %-8s | %-8s %-9s %-10s | %-8s %-9s %-10s %-8s\n" deck seed ladLP m0LP "d0-lad" m0_TS m1LP "d1-lad" m1_TS "TS1/TS0"
for deck in $DECKS; do
  sumd0=0; sumd1=0; n=0
  for s in $SEEDS; do
    read lLP lTS < <(run "$LAD"  "$deck" "$s" "")
    read a0 t0   < <(run "$COMB" "$deck" "$s" "MTG_ESC_CLIMB_GROWTH=0")
    read a1 t1   < <(run "$COMB" "$deck" "$s" "MTG_ESC_CLIMB_GROWTH=1")
    d0=$(awk -v a="$a0" -v l="$lLP" 'BEGIN{printf "%+.4f",a-l}')
    d1=$(awk -v a="$a1" -v l="$lLP" 'BEGIN{printf "%+.4f",a-l}')
    wr=$(awk -v x="$t1" -v y="$t0" 'BEGIN{if(y>0)printf "%.1f%%",100*x/y;else print"-"}')
    printf "%-9s %-6s | %-8s | %-8s %-9s %-10s | %-8s %-9s %-10s %-8s\n" "$deck" "$s" "$lLP" "$a0" "$d0" "$t0" "$a1" "$d1" "$t1" "$wr"
    sumd0=$(awk -v x="$sumd0" -v a="$a0" -v l="$lLP" 'BEGIN{printf "%.4f",x+(a-l)}')
    sumd1=$(awk -v x="$sumd1" -v a="$a1" -v l="$lLP" 'BEGIN{printf "%.4f",x+(a-l)}')
    n=$((n+1))
  done
  printf ">> %-6s mean dLP-vs-ladder: mode0=%s  mode1=%s  (n=%s seeds; smaller|.|=more neutral)\n" \
    "$deck" "$(awk -v s=$sumd0 -v n=$n 'BEGIN{printf "%+.4f",s/n}')" "$(awk -v s=$sumd1 -v n=$n 'BEGIN{printf "%+.4f",s/n}')" "$n"
done
echo DONE

#!/usr/bin/env bash
# Root-cause the overnight searched SLOWER games (a game becoming unwon = the maximal slowdown under
# loss=max_turns+1): re-run each single-game at 1x/4x/16x its case budget
# (budget-churn check) and once at a LIFTED horizon (max_turns=16) at 1x budget (horizon-edge check).
# recovers to a win at higher budget => budget churn (benign); wins only at lifted horizon => horizon-edge
# (benign, the marginally-slower line just crosses T8); stays -1 everywhere => variance/real (dig further).
set -uo pipefail
BIN=build/Release/mtg
win() { # deck prof seed gi budget maxturns  -> win turn (-1 loss)
  # value_play profile drives depth=5 + single-depth escalation; omit --depth, override budget only.
  MTG_DUMP_WINS=1 "$BIN" "$1" --profile "$2" --seed "$3" --games 1 --game-index "$4" \
    --budget-ms "$5" --max-turns "$6" --lookahead-bottoming --threads 1 2>&1 \
    | grep -oP '\[win\] gi=[0-9]+ wt=\K-?[0-9]+' | head -1
}
declare -A F=( [antilife]=decks/Anti-Lifegain/Anti-Lifegain.cod [hinata]=decks/Hinata2/Hinata2.cod [th]=decks/treasure_hunt/treasure_hunt.txt )
declare -A P=( [antilife]=decks/Anti-Lifegain/Anti-Lifegain.profile.json [hinata]=decks/Hinata2/Hinata2.profile.json [th]=decks/treasure_hunt/treasure_hunt.profile.json )
# deck seed gi budget  (the 8 overnight slower games)
CASES=(
  "th 6006 590 80"  "th 6006 662 80"
  "antilife 5005 330 20" "antilife 5005 334 20" "antilife 5005 854 20" "antilife 7007 801 20"
  "hinata 6006 104 20" "hinata 7007 12 20"
)
printf "%-9s %-6s %-5s | 1x    4x    16x  | lifted(H16,1x)\n" DECK SEED GI
for c in "${CASES[@]}"; do
  read deck seed gi b <<< "$c"
  gseed=$(( seed + gi ))   # per-game seed (base + gi), matching the batch reproduction
  w1=$(win "${F[$deck]}" "${P[$deck]}" "$gseed" "$gi" "$b" 8)
  w4=$(win "${F[$deck]}" "${P[$deck]}" "$gseed" "$gi" "$((b*4))" 8)
  w16=$(win "${F[$deck]}" "${P[$deck]}" "$gseed" "$gi" "$((b*16))" 8)
  wh=$(win "${F[$deck]}" "${P[$deck]}" "$gseed" "$gi" "$b" 16)
  printf "%-9s %-6s %-5s | %-5s %-5s %-5s | %s\n" "$deck" "$seed" "$gi" "${w1:-NA}" "${w4:-NA}" "${w16:-NA}" "${wh:-NA}"
done
echo DONE

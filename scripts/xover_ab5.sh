#!/bin/bash
# A/B: table-driven crossover (default) vs old uniform offset-3 (MTG_VALUE_TRUST_OFFSET=3), same binary, d5.
cd /workspaces/MagicDeckTester2
BIN=${BIN:-build_xover/Release/mtg}
declare -A DECK=([antilife]=Anti-Lifegain/Anti-Lifegain.cod [slivers]=slivers_vial/slivers_vial.txt [TH]=treasure_hunt/treasure_hunt.txt [burn]=burn/burn.txt [knights]=Knights/Knights.cod)
fp(){ $BIN "decks/$1" --games "${GAMES:-300}" --seed "$2" --depth 5 --max-turns 8 --threads 20 2>/dev/null | grep -E 'Games won|Avg win turn' | grep -oE '[0-9]+\.?[0-9]*' | tr '\n' '/'; }
echo "deck seed OLD_off3 NEW_table changed"
for dk in antilife slivers TH burn knights; do for s in 1001 2002; do
  o=$(MTG_VALUE_TRUST_OFFSET=3 fp "${DECK[$dk]}" "$s"); n=$(fp "${DECK[$dk]}" "$s")
  [ "$o" = "$n" ] && c=same || c=DIFF
  echo "$dk $s $o $n $c"
done; done
echo AB5_DONE

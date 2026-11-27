#!/bin/bash
# featall.sh <list "Deck gi" per line> <out>: gdiff (off vs onA) + feat.py per game.
SC=/workspaces/MagicDeckTester/build/scratch/elves-lordcast
while read D g; do
  bash logs/lordcast/gdiff.sh decks/$D $g "MTG_ATTACK_LORD_CAST=0" "MTG_ATTACK_LORD_CAST=1 MTG_LORDCAST_HAND=1" > /dev/null 2>&1
  echo "$D $g $(python3 logs/lordcast/feat.py $SC/gd/${D}_$g/a $SC/gd/${D}_$g/b)"
done < $1 > $2

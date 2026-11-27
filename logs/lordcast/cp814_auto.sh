#!/bin/bash
# cp814_auto.sh: game 814 (Elves 2HG seed 9850815) handed to the search at the FULL arm's T4 pre-main board,
# prune off vs on, d5 b20 and d8 b0. Output build/scratch/elves-scal2/cp814/<prune>_<shape>/.
S=/workspaces/MagicDeckTester/build/scratch/elves-scal2; CSV="0,1,1,0,0,0,0,0,0,7,0,0"
cd /workspaces/MagicDeckTester/.claude/worktrees/elves-scal2 || exit 1
for P in 0 1; do for SH in "5 20" "8 0"; do set -- $SH; O=$S/cp814/p${P}_d$1b$2; rm -rf $O; mkdir -p $O
  env MTG_START_LIFE=30 MTG_OPPONENT_HEADS=2 MTG_ELVES_SCALING_PRUNE=$P MTG_ELVES_SCALING_TRACE=4 $S/ab/mtg3 decks/Elves/Elves.cod \
    --profile decks/Elves/Elves.profile.json --claude-play --seed 9850815 --max-turns 8 --choices "$CSV" --choices-then-auto \
    --depth $1 --budget-ms $2 --log-dir $O > $O/out.txt 2> $O/err.txt; echo "P=$P d$1b$2 rc=$? $(grep -i 'win\|then-auto' $O/out.txt | tail -2 | tr '\n' ' ')"
done; done

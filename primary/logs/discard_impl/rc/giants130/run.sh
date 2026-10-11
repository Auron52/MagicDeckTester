#!/bin/bash
# Giants regression d3 s3003 gi130 (seed 3133): policy on vs off, with game logs.
R=/workspaces/MagicDeckTester
O=$R/logs/discard_impl/rc/giants130
for arm in 0 1; do
  mkdir -p $O/log_$arm
  (cd $R; MTG_GIANTS_BUCKET_DISCARD=$arm nice -n 19 $R/build/PGO/mtg decks/Giants/Giants.cod --profile decks/Giants/Giants.profile.json --games 1 --seed 3133 --game-index 130 --depth 3 --budget-ms 10 --ignore-play-profile --threads 1 --log-dir $O/log_$arm > $O/out_$arm.txt 2> $O/err_$arm.txt)
  echo "arm=$arm: $(grep 'avg (turns)' $O/out_$arm.txt)"
done

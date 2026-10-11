#!/bin/bash
# Executor tap trace, both arms.
R=/workspaces/MagicDeckTester
O=$R/logs/discard_impl/rc/dragonstorm
for arm in on off; do
  if [ $arm = on ]; then F=1; else F=0; fi
  mkdir -p $O/et_$arm
  (cd $R; MTG_DRAGONSTORM_BUCKET_DISCARD=$F MTG_EXEC_TAP_TRACE=1 nice -n 19 $R/build/PGO/mtg decks/Dragonstorm/Dragonstorm.cod --profile decks/Dragonstorm/Dragonstorm.profile.json --games 1 --seed 1534 --game-index 533 --depth 0 --budget-ms 0 --ignore-play-profile --threads 1 --log-dir $O/et_$arm > $O/et_out_$arm.txt 2> $O/et_err_$arm.txt)
done

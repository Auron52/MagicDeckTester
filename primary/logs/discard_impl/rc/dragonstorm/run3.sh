#!/bin/bash
# Single-game CLI run with --trace and plans trace, both arms.
R=/workspaces/MagicDeckTester
O=$R/logs/discard_impl/rc/dragonstorm
for arm in on off; do
  if [ $arm = on ]; then F=1; else F=0; fi
  mkdir -p $O/tr_$arm
  (cd $R; MTG_DRAGONSTORM_BUCKET_DISCARD=$F MTG_TRACE=discard,plans,tie nice -n 19 $R/build/PGO/mtg decks/Dragonstorm/Dragonstorm.cod --profile decks/Dragonstorm/Dragonstorm.profile.json --games 1 --seed 1534 --game-index 533 --depth 0 --budget-ms 0 --ignore-play-profile --threads 1 --trace --log-dir $O/tr_$arm > $O/tr_out_$arm.txt 2> $O/tr_err_$arm.txt)
done
wc -l $O/tr_out_on.txt $O/tr_err_on.txt $O/tr_out_off.txt $O/tr_err_off.txt

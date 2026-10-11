#!/usr/bin/env bash
R=/workspaces/MagicDeckTester; E=$R/logs/discard_impl/rc3/heldout; BIN=$R/logs/discard_impl/ab3/mtg_ab3_d6a760e5
cd $R
nice -n 10 $BIN --batch $E/m.json --threads 2 --game-log-dir $E/g > $E/out 2> $E/err
echo done > $E/DONE

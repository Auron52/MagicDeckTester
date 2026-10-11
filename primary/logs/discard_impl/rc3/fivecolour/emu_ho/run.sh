#!/usr/bin/env bash
R=/workspaces/MagicDeckTester; E=$R/logs/discard_impl/rc3/fivecolour/emu_ho; BIN=$R/logs/discard_impl/ab3/mtg_ab3_d6a760e5
cd $R
nice -n 10 $BIN --batch $E/m.json --threads 2 --game-log-dir $E/u > $E/u.out 2> $E/u.err &
nice -n 10 $BIN --batch $E/m.json --threads 2 --cards-json $E/../cards_yield5.json --game-log-dir $E/p > $E/p.out 2> $E/p.err &
wait
echo done > $E/DONE

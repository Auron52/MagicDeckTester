#!/bin/bash
D=/workspaces/MagicDeckTester/logs/discard_impl/rc/giants
cd /workspaces/MagicDeckTester || exit 1
mkdir -p $D/glog $D/gtrace
MTG_TRACE=discard MTG_DUMP_WINS=1 nice -n 19 /workspaces/MagicDeckTester/build/PGO/mtg --batch $D/manifest.json --threads 1 --game-log-dir $D/glog --game-trace-dir $D/gtrace > $D/batch2.out 2> $D/batch2.err
grep -i "\[win\]" $D/batch2.err
find $D/glog $D/gtrace -type f | head -20

#!/bin/bash
D=/workspaces/MagicDeckTester/logs/discard_impl/rc/giants
cd /workspaces/MagicDeckTester || exit 1
MTG_DUMP_WINS=1 nice -n 19 /workspaces/MagicDeckTester/build/PGO/mtg --batch $D/manifest.json --threads 1 > $D/batch.out 2> $D/batch.err
grep -i "win" $D/batch.err | head -20

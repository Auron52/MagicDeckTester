#!/bin/bash
# Build the primary checkout (shared slot, cores 0-5, nice 19) and run the unit tests the same way.
R=/workspaces/MagicDeckTester
O=$R/build/scratch/primary
mkdir -p $O
echo "tip $(git -C $R rev-parse --short HEAD) src $(git -C $R rev-parse --short HEAD:src)"
bash $R/build/scratch/wt_build.sh $R > $O/build.log 2>&1; rc=$?
tail -n 3 $O/build.log
[ $rc -ne 0 ] && { echo "BUILD FAILED rc=$rc"; grep -n "error" $O/build.log | head -n 20; exit 1; }
bash $R/build/scratch/lowprio.sh $R/build/Release/mtg-test > $O/unit.log 2>&1; rc=$?
tail -n 4 $O/unit.log
echo "unit rc=$rc"

#!/bin/bash
S=/workspaces/MagicDeckTester/build/scratch/bs-oracle
cd /workspaces/MagicDeckTester/.claude/worktrees/bs-oracle
./build.sh release -- --target mtg-test > $S/build-test.log 2>&1 || { echo "build rc=$?" > $S/units.DONE; exit 1; }
build/Release/mtg-test -tc='*share one frontier payment table*' > $S/unit_new.log 2>&1; echo "new rc=$?" >> $S/unit_new.log
UNIT_PAR_DIR=$S/unit_par bash scripts/unit_par.sh build/Release/mtg-test 2 > $S/unit_par.log 2>&1
echo "units rc=$?" > $S/units.DONE

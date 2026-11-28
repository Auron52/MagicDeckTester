#!/bin/bash
S=/workspaces/MagicDeckTester/build/scratch/bs-oracle
cd /workspaces/MagicDeckTester/.claude/worktrees/bs-oracle
sleep 200
flock /workspaces/MagicDeckTester/build/scratch/claude_build.lock ./build.sh > $S/build-lf.log 2>&1 || { echo "build rc=$?" > $S/stage1.DONE; exit 1; }
cp build/Release/mtg $S/mtg.lf
build/Release/mtg-test -tc='*leftover fold*,*share one frontier payment table*' > $S/unit_lf.log 2>&1; echo "unit rc=$?" >> $S/unit_lf.log
E="MTG_PGO=0 MTG_MANA_ORACLE=2 MTG_MANA_ORACLE_DECISION_S=40 MTG_ORACLE_LEFTOVER_FOLD_CHECK=1"
env $E MTG_MANA_ORACLE_ONLY_TURN=4 MTG_MANA_ORACLE_ONLY_MENU=413 $S/mtg.lf --batch $S/m531.json --threads 1 > $S/c531.out 2> >(grep -E "leftover-fold|leftover fold|DECISION-COST|^=== MANA ORACLE(:| classes)" > $S/c531.err) &
env $E MTG_MANA_ORACLE_ONLY_TURN=7 $S/mtg.lf --batch $S/m46.json --threads 1 > $S/c46.out 2> >(grep -E "leftover-fold|leftover fold|DECISION-COST|^=== MANA ORACLE(:| classes)" > $S/c46.err) &
wait
echo done > $S/stage1.DONE

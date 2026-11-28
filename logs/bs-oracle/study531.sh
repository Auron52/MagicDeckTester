#!/bin/bash
S=/workspaces/MagicDeckTester/build/scratch/bs-oracle
cd /workspaces/MagicDeckTester/.claude/worktrees/bs-oracle
E="MTG_PGO=0 MTG_MANA_ORACLE=2 MTG_MANA_ORACLE_ONLY_TURN=4 MTG_MANA_ORACLE_ONLY_MENU=413 MTG_MANA_ORACLE_COST_TRACE=1 MTG_MANA_ORACLE_HEAVY=1"
env $E $S/mtg.base --batch $S/m531.json --threads 1 > $S/s531_base.out 2> $S/s531_base.err &
env $E perf record -F 99 -o $S/s531_memo.perf $S/mtg.memo --batch $S/m531.json --threads 1 > $S/s531_memo.out 2> $S/s531_memo.err &
wait
echo done > $S/study531.DONE

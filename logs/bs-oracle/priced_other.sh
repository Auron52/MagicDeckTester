#!/bin/bash
S=/workspaces/MagicDeckTester/build/scratch/bs-oracle
cd /workspaces/MagicDeckTester/.claude/worktrees/bs-oracle
run() { env MTG_PGO=0 MTG_MANA_ORACLE=2 MTG_MANA_ORACLE_HEAVY=1 $2 --batch $S/mother.json --threads 1 > $S/$1.out 2> >(grep -E 'DECISION-COST|^=== MANA ORACLE(:| leftover)' > $S/$1.err); }
run qA $S/mtg.memo & run qB $S/mtg.lf & wait
echo done > $S/priced_other.DONE

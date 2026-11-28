#!/bin/bash
S=/workspaces/MagicDeckTester/build/scratch/bs-oracle
cd /workspaces/MagicDeckTester/.claude/worktrees/bs-oracle
run() { env MTG_PGO=0 MTG_MANA_ORACLE=2 $2 --batch $S/mother.json --threads 1 > $S/$1.out 2> >(grep -E '^=== MANA ORACLE(:| leftover)' > $S/$1.err); }
run pA1 $S/mtg.memo & run pB1 $S/mtg.lf & wait
run pB2 $S/mtg.lf & run pA2 $S/mtg.memo & wait
echo done > $S/pair_other.DONE

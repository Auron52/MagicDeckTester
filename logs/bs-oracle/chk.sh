#!/bin/bash
S=/workspaces/MagicDeckTester/build/scratch/bs-oracle
cd /workspaces/MagicDeckTester/.claude/worktrees/bs-oracle
MTG_PGO=0 MTG_MANA_ORACLE=2 MTG_ORACLE_PAY_TABLE_MEMO_CHECK=1 $S/mtg.memo --batch $S/mchk.json --threads 1 > $S/chk.out 2> >(grep -E 'pay-table|^=== MANA ORACLE(:| pay)' > $S/chk.err)
echo "rc=$?" > $S/chk.DONE

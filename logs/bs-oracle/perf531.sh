#!/bin/bash
S=/workspaces/MagicDeckTester/build/scratch/bs-oracle
cd /workspaces/MagicDeckTester/.claude/worktrees/bs-oracle
env MTG_PGO=0 MTG_MANA_ORACLE=2 MTG_MANA_ORACLE_ONLY_TURN=4 MTG_MANA_ORACLE_ONLY_MENU=413 MTG_MANA_ORACLE_HEAVY=1 perf record -F 49 -g -o $S/p531lf.perf $S/mtg.lf --batch $S/m531.json --threads 1 > $S/p531lf.out 2> >(grep -E "DECISION-COST|^=== MANA ORACLE" > $S/p531lf.err)
perf report -i $S/p531lf.perf --no-children --stdio 2>/dev/null | grep -v "^#" | grep "%" | head -60 > $S/p531lf.self.txt
perf report -i $S/p531lf.perf --children --stdio 2>/dev/null | grep -v "^#" | grep -E "^ +[0-9]" | head -80 > $S/p531lf.children.txt
echo done > $S/perf531.DONE

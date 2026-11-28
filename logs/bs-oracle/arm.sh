#!/bin/bash
# arm.sh <bin> <oracle level> <manifest> <tag> [ENV=VAL...]: one pooled batch on 2 threads; DONE marker $S/<tag>.DONE
S=/workspaces/MagicDeckTester/build/scratch/bs-oracle
B=$1; L=$2; M=$3; T=$4; shift 4
cd /workspaces/MagicDeckTester/.claude/worktrees/bs-oracle
env "$@" MTG_PGO=0 MTG_MANA_ORACLE=$L $B --batch $M --threads 2 > $S/$T.out 2> >(grep -v '^\[mana-oracle\] \(DECISION\|PLAN-COST\|GATE-COST\) ' > $S/$T.err)
echo "rc=$?" > $S/$T.DONE

#!/bin/bash
# runnew.sh <manifest-stem> <threads> [VAR=VAL ...] : new binary, this tree, scratch output dir <stem>/
A=/workspaces/MagicDeckTester/build/scratch/claude_ovn1120/agent
N=/workspaces/MagicDeckTester/.claude/worktrees/agent-a1088baf277f40b58
NEWBIN=$(readlink -f /workspaces/MagicDeckTester/test/logs/overnight/mtg.run)
stem=$1; th=$2; shift 2
mkdir -p $A/$stem
cd $N
env MTG_PGO=0 MTG_RSS_CAP_GB=${CAP:-8} "$@" "$NEWBIN" --batch $A/$stem.json --cards-json $A/cards_c86.json --threads $th --game-log-dir $A/$stem/wins > $A/$stem/batch.out 2> $A/$stem/batch.err
echo "rc=$?" > $A/$stem/DONE

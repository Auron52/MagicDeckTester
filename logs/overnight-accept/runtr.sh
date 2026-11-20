#!/bin/bash
# runtr.sh <manifest-stem> : both arms, 2 threads each, traces into trold/ trnew/ (old arm in the old tree)
A=/workspaces/MagicDeckTester/build/scratch/claude_ovn1120/agent
O=/workspaces/MagicDeckTester/.claude/worktrees/ovn1120-old
N=/workspaces/MagicDeckTester/.claude/worktrees/agent-a1088baf277f40b58
NEWBIN=$(readlink -f /workspaces/MagicDeckTester/test/logs/overnight/mtg.run)
OLDBIN=$(readlink -f $O/build/Release/mtg)
s=$1
export MTG_PGO=0 MTG_RSS_CAP_GB=6
( cd $O && "$OLDBIN" --batch $A/$s.json --threads 2 --game-log-dir $A/trold/wins_$s --game-trace-dir $A/trold/traces > $A/trold/$s.out 2> $A/trold/$s.err ) &
( cd $N && "$NEWBIN" --batch $A/$s.json --threads 2 --game-log-dir $A/trnew/wins_$s --game-trace-dir $A/trnew/traces > $A/trnew/$s.out 2> $A/trnew/$s.err ) &
wait
touch $A/$s.DONE

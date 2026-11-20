#!/bin/bash
# Phase 2 (run under the box lock): one big new-binary batch (artifact-swap rows), and beside it two
# small 4-thread case-setting trace batches (old arm in the old tree, new arm in this tree).
A=/workspaces/MagicDeckTester/build/scratch/claude_ovn1120/agent
O=/workspaces/MagicDeckTester/.claude/worktrees/ovn1120-old
N=/workspaces/MagicDeckTester/.claude/worktrees/agent-a1088baf277f40b58
NEWBIN=$(readlink -f /workspaces/MagicDeckTester/test/logs/overnight/mtg.run)
OLDBIN=$(readlink -f $O/build/Release/mtg)
export MTG_PGO=0
mkdir -p $A/art $A/trnew $A/trold
( cd $O && MTG_RSS_CAP_GB=6 "$OLDBIN" --batch $A/trace_old.json --threads 4 --game-log-dir $A/trold/wins --game-trace-dir $A/trold/traces > $A/trold/batch.out 2> $A/trold/batch.err; echo "rc=$?" > $A/trold/DONE ) &
( cd $N && MTG_RSS_CAP_GB=6 "$NEWBIN" --batch $A/trace_new.json --threads 4 --game-log-dir $A/trnew/wins --game-trace-dir $A/trnew/traces > $A/trnew/batch.out 2> $A/trnew/batch.err; echo "rc=$?" > $A/trnew/DONE ) &
( cd $N && MTG_RSS_CAP_GB=30 "$NEWBIN" --batch $A/art.json --threads $(( $(nproc) - 8 )) --game-log-dir $A/art/wins > $A/art/batch.out 2> $A/art/batch.err; echo "rc=$?" > $A/art/DONE ) &
wait
touch $A/PHASE2_DONE

#!/bin/bash
# Both arms of the PERSISTS d8 b0 re-run, concurrently under ONE box lock (two binaries cannot share
# one --batch; running them side by side lets each fill the other's tail). Memory: 2 x 20 GB cap.
A=/workspaces/MagicDeckTester/build/scratch/claude_ovn1120/agent
O=/workspaces/MagicDeckTester/.claude/worktrees/ovn1120-old
N=/workspaces/MagicDeckTester/.claude/worktrees/agent-a1088baf277f40b58
NEWBIN=$(readlink -f /workspaces/MagicDeckTester/test/logs/overnight/mtg.run)
OLDBIN=$(readlink -f $O/build/Release/mtg)
TH=$(nproc)
mkdir -p $A/old $A/new
export MTG_RSS_CAP_GB=20 MTG_PGO=0
( cd $O && "$OLDBIN" --batch $A/d8b0_old.json --threads $TH --game-log-dir $A/old/wins > $A/old/batch.out 2> $A/old/batch.err; echo "rc=$?" > $A/old/DONE ) &
P1=$!
( cd $N && "$NEWBIN" --batch $A/d8b0.json --threads $TH --game-log-dir $A/new/wins > $A/new/batch.out 2> $A/new/batch.err; echo "rc=$?" > $A/new/DONE ) &
P2=$!
echo "old=$P1 new=$P2" > $A/pids
wait $P1 $P2
touch $A/ALL_DONE

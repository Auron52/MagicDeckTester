#!/bin/bash
# heath_ab.sh: NEW binary on the 6 traced Five Colour d0 games, with c86 cards.json vs c86 + Windswept Heath reverted to its
# pre-9344fa594 legacy immediate-fetch record. 1 lane slot, 1 thread each, serial.
A=/workspaces/MagicDeckTester/build/scratch/claude_ovn1120/agent
N=/workspaces/MagicDeckTester/.claude/worktrees/agent-a1088baf277f40b58
NEWBIN=$(readlink -f /workspaces/MagicDeckTester/test/logs/overnight/mtg.run)
cd $N
for v in cards_c86 cards_c86_heathlegacy; do
  rm -rf $A/heath/$v; mkdir -p $A/heath/$v
  MTG_PGO=0 bash scripts/box_lane.sh --slots 1 --cap-gb 4 -- "$NEWBIN" --batch $A/fc_tr.json --cards-json $A/$v.json --threads 1 --game-log-dir $A/heath/$v/wins --game-trace-dir $A/heath/$v/traces > $A/heath/$v/out 2> $A/heath/$v/err
done
touch $A/heath/DONE

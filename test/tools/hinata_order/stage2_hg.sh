#!/usr/bin/env bash
# stage2_hg.sh GI -- stage 2 (--depth 8 --budget-ms 0, unlimited) for one 2HG-shipped slower game, NEW arm.
S=/workspaces/MagicDeckTester/build/scratch/hinata-order
T=/workspaces/MagicDeckTester/.claude/worktrees/agent-a4d121dd522b89155/test/tools/hinata_order
gi=$1
seed=$((11150001 + gi))
echo "gi=$gi stage2 d8b0 new: $(bash "$T/onegame2.sh" "$S/games/hg${gi}_d8b0_o1" "$seed" "$gi" 1 1 --depth 8 --budget-ms 0 --ignore-play-profile)"

#!/bin/bash
# The tier's only slower game: Maze's End smoke d0 s1001 gi592 (T5 -> T7). Both arms (levers on / off) on the t2 binary:
# first at d0 (does the off arm reproduce GT's T5?), then at --depth 8 --budget-ms 0. Run inside a 2-slot lane job.
W=/workspaces/MagicDeckTester/.claude/worktrees/agent-a1651726d5b941608
D=$W/build/scratch/claude_merfolk_gate/d8b0_t2
mkdir -p $D
cd $W || exit 1
B=$W/build/scratch/claude_merfolk_gate/t2/mtg.final
run() { env MTG_SCALED_LAND_ROUTE=$2 MTG_PAY_REFINE_SCALED=$2 $B "decks/Maze's End/Maze's End.cod" \
  --profile "decks/Maze's End/Maze's End.profile.json" --games 1 --seed 1593 --game-index 592 \
  $3 > $D/$1.out 2> $D/$1.err; echo "$1 rc=$? $(date -u +%H:%M)" >> $D/status; }
run d0_on 1 "--depth 0" & run d0_off 0 "--depth 0" & wait
run d8_on 1 "--depth 8 --budget-ms 0" & run d8_off 0 "--depth 8 --budget-ms 0" & wait
touch $D/DONE

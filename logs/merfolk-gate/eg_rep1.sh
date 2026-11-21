#!/bin/bash
# Option (a)'s two new APPLY-DROP game-turns from slice_eg (s30000034 T5, s30000075 T5): one game each, arm a, oracle
# level 2 on that turn, exec tap trace and a game log. Run inside a 2-slot lane job.
W=/workspaces/MagicDeckTester/.claude/worktrees/agent-a1651726d5b941608
E=$W/build/scratch/claude_merfolk_gate/eg
T=$W/build/scratch/claude_merfolk_gate/gatetree
mkdir -p $E/rep1
cd $T || exit 1
run() { gi=$(( $1 - 30000000 )); env MTG_MANA_ORACLE=2 MTG_MANA_ORACLE_TURN=5 MTG_EXEC_TAP_TRACE=1 MTG_SCALED_GROWTH_BOUND=1 \
  MTG_SCALED_GROWER_DECIDER=$2 MTG_MERFOLK_GROWER_ORDER=0 $E/mtg.v2 "decks/Merfolk Turns/Merfolk Turns.cod" \
  --profile "decks/Merfolk Turns/Merfolk Turns.profile.json" --games 1 --seed $1 --game-index $gi \
  --log-dir $E/rep1/log_$1_$2 > $E/rep1/$1_$2.out 2> $E/rep1/$1_$2.err; }
run 30000034 1 & run 30000075 1 & wait
touch $E/rep1/DONE

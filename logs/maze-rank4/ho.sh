#!/bin/bash
# ho.sh seed gi arm tag choices [ENV=V...] -> h<gi>_<arm>_<tag>.{out,err}
S=/workspaces/MagicDeckTester/build/scratch/maze-rank-2hg
B=${BIN:-/workspaces/MagicDeckTester/build/scratch/claude_maze_rank3/abtb/bin/mtg}
W=${WT:-/workspaces/MagicDeckTester/.claude/worktrees/maze-rank3}
seed=$1; gi=$2; arm=$3; tag=$4; ch=$5; shift 5
case $arm in
 base) A="MTG_RANK_OBJECT_COLORS=0 MTG_MAZE_RANK_SPENDABLE=0";;
 new) A="MTG_RANK_OBJECT_COLORS=1 MTG_MAZE_RANK_SPENDABLE=1";;
 rank) A="MTG_RANK_OBJECT_COLORS=1 MTG_MAZE_RANK_SPENDABLE=0";;
esac
cd $W
env MTG_PGO=0 MTG_MAZE_HOLD_UP=0 MTG_START_LIFE=30 MTG_OPPONENT_HEADS=2 $A "$@" $B "decks/Maze's End/Maze's End.cod" --profile "decks/Maze's End/Maze's End.profile.json" --claude-play --seed $seed --game-index $gi --max-turns 10 --choices "$ch" --choices-then-auto --depth 8 --budget-ms 0 --ignore-play-profile > $S/h${gi}_${arm}_$tag.out 2> $S/h${gi}_${arm}_$tag.err
echo "$gi $arm $tag: $(grep -o '"win_turn": [0-9]*' $S/h${gi}_${arm}_$tag.out)"

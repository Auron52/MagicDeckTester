#!/bin/bash
# run1.sh <seed> <gi> <arm base|new|rank> <tag> [ENV=V...]  (2HG d8 b0 single game) BIN/WT env override
S=/workspaces/MagicDeckTester/build/scratch/maze-rank-2hg
B=${BIN:-/workspaces/MagicDeckTester/build/scratch/claude_maze_rank3/abtb/bin/mtg}
W=${WT:-/workspaces/MagicDeckTester/.claude/worktrees/maze-rank3}
seed=$1; gi=$2; arm=$3; tag=$4; shift 4
case $arm in
 base) A="MTG_RANK_OBJECT_COLORS=0 MTG_MAZE_RANK_SPENDABLE=0";;
 new) A="MTG_RANK_OBJECT_COLORS=1 MTG_MAZE_RANK_SPENDABLE=1";;
 rank) A="MTG_RANK_OBJECT_COLORS=1 MTG_MAZE_RANK_SPENDABLE=0";;
esac
LIFE=${LIFE:-"MTG_START_LIFE=30 MTG_OPPONENT_HEADS=2"}
cd $W
env MTG_PGO=0 MTG_MAZE_HOLD_UP=0 $LIFE $A "$@" $B "decks/Maze's End/Maze's End.cod" --profile "decks/Maze's End/Maze's End.profile.json" \
  --games 1 --seed $seed --game-index $gi --depth ${DEPTH:-8} --budget-ms ${BUD:-0} --ignore-play-profile --threads 1 \
  > $S/g${gi}_${arm}_$tag.out 2> $S/g${gi}_${arm}_$tag.err
echo "rc=$?" >> $S/g${gi}_${arm}_$tag.out

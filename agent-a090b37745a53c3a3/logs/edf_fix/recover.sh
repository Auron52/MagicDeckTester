#!/bin/bash
# Run the listed EDF games on a binary, one arm, print the win turn per game.
# usage: recover.sh <binary> <tag> <arm: new|old> [games-file]
#   games-file lines: <cell> <gi> <seed>   (default: the E1-E4 games)
# Extra env: ENV_EXTRA (passed through to run1.sh).
W=/workspaces/MagicDeckTester/.claude/worktrees/agent-a090b37745a53c3a3/logs/edf_fix
BIN=$1; TAG=$2; ARM=$3; LIST=${4:-$W/games_e.txt}
while read -r cell gi seed; do
  [ -z "$gi" ] && continue
  out=$W/rec_${TAG}_${ARM}_g${gi}_${cell}
  r=$(bash $W/run1.sh "$cell" "$gi" "$seed" "$ARM" "$out" "$BIN" | grep -o "avg (turns)   : [0-9.]*" | awk '{print $NF}')
  echo "$TAG $ARM $cell gi=$gi seed=$seed turns=$r"
done < "$LIST"

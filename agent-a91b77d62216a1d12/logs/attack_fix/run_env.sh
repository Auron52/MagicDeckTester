#!/usr/bin/env bash
# run_env.sh <bin> <deck> <cell> <gi> <seed> <logdir> <envfile|-> [KEY=VAL ...] [-- extra mtg args]
# One game with the env levers listed one per line in <envfile> plus any KEY=VAL args; JSON log to
# <logdir>, stdout/stderr to <logdir>/out, <logdir>/err.
W=/workspaces/MagicDeckTester/.claude/worktrees/agent-a91b77d62216a1d12
bin=$1; deck=$2; cell=$3; gi=$4; seed=$5; logdir=$6; envfile=$7; shift 7
mkdir -p "$logdir"
if [ "$envfile" != - ]; then
  while IFS= read -r x; do [ -n "$x" ] && export "$x"; done < "$envfile"
fi
while [ $# -gt 0 ] && [ "$1" != -- ]; do export "$1"; shift; done
[ "${1:-}" = -- ] && shift
BIN=$bin "$W/logs/attack_fix/rg.sh" "$deck" "$cell" "$gi" "$seed" "$logdir" "$@" > "$logdir/out" 2> "$logdir/err"
grep 'avg (turns)' "$logdir/out"

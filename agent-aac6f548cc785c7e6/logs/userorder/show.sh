#!/usr/bin/env bash
# show.sh <relpath-under-src/ai or abs> start end [start end ...]
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6
f="$1"; shift
case "$f" in
  /*) ;;
  *) f="$WT/src/ai/$f" ;;
esac
while [ "$#" -ge 2 ]; do
  echo "----- $1-$2"
  sed -n "$1,$2p" -- "$f"
  shift 2
done

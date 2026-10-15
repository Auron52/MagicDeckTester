#!/usr/bin/env bash
# A scenario and its battlefield-permuted twin under scn4.sh (ofix vs off), printing the T<turn> lines.
# usage: scn_layouts.sh <binary> <out_root> <scenario.json> [<scenario2.json> ...] -- [ENV=VAL ...]
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9d0a1cbf3361f164
BIN=$1; ROOT=$2; shift 2
FILES=()
while [ $# -gt 0 ] && [ "$1" != "--" ]; do FILES+=("$1"); shift; done
[ "${1:-}" = "--" ] && shift
for f in "${FILES[@]}"; do
  b=$(basename "$f" .json)
  echo "## $b"
  bash "$WT/test/tools/stompy_order_ab/scn4.sh" "$BIN" "$f" "$ROOT/$b" "$@" | grep -E '^==|MAIN_1|COMBAT'
done

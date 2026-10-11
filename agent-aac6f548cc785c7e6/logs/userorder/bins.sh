#!/usr/bin/env bash
# bins.sh <seed> <gi> <depth> -- one Dragonstorm game's win turn on every saved binary of this branch
U=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6/logs/userorder
seed="$1"; gi="$2"; depth="$3"
for b in base 9d08 f2 f1 f3 cco p2 final; do
  printf "%-6s " "$b"
  bash "$U/combo.sh" "$U/mtg.$b" "$seed" "$gi" "$depth" -
done

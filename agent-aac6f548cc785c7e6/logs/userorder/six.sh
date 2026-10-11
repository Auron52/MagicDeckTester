#!/usr/bin/env bash
# six.sh <bin> <env-set>... -- the 6 ab3-slower d0 games (+ the two earlier slower ones) per env set
U=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6/logs/userorder
BIN="$1"; shift
SETS=("$@")
for pair in "445 1190446" "1011 1191012" "1412 1191413" "1590 1191591" "1686 1191687" "1701 1191702" "10 7600011" "232 7600233"; do
  read -r gi seed <<< "$pair"
  bash "$U/combo.sh" "$BIN" "$seed" "$gi" 0 "${SETS[@]}"
done

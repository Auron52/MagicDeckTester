#!/usr/bin/env bash
# probe_pair.sh BASE HG(0/1) GI... -- replay each game old (order off) and new (order on) at the deck's shipped
# settings, single thread, niced, all files on build/scratch (RULES items 4 and 9); print the side-by-side diff.
S=/workspaces/MagicDeckTester/build/scratch/hinata-order
T=/workspaces/MagicDeckTester/.claude/worktrees/agent-a4d121dd522b89155/test/tools/hinata_order
base=$1; hg=$2; shift 2
tag=h; [ "$hg" = 1 ] && tag=hg
for gi in "$@"; do
  seed=$((base + gi))
  for arm in 0 1; do
    bash "$T/onegame2.sh" "$S/games/${tag}${gi}_ship_o$arm" "$seed" "$gi" "$hg" "$arm" > /dev/null
  done
  echo "================ ${tag} gi=$gi seed=$seed"
  python3 "$T/showgame.py" "$S/games/${tag}${gi}_ship_o0/"*.json "$S/games/${tag}${gi}_ship_o1/"*.json | cut -c1-560
done

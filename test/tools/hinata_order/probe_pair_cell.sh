#!/usr/bin/env bash
# probe_pair_cell.sh CELL GI... -- replay each game of CELL (hinata_ship / hinata_d3 / hinata2hg_d3 / ...) old and
# new at the CELL's own settings (single thread, niced, files on build/scratch); print the side-by-side diff.
S=/workspaces/MagicDeckTester/build/scratch/hinata-order
T=/workspaces/MagicDeckTester/.claude/worktrees/agent-a4d121dd522b89155/test/tools/hinata_order
cell=$1; shift
case "$cell" in
  hinata_ship) base=11100001; hg=0; extra=() ;;
  hinata_d3) base=11110001; hg=0; extra=(--depth 3 --budget-ms 10 --ignore-play-profile) ;;
  hinata_d0) base=11120001; hg=0; extra=(--depth 0 --budget-ms 0 --ignore-play-profile) ;;
  hinata2hg_ship) base=11150001; hg=1; extra=() ;;
  hinata2hg_d3) base=11160001; hg=1; extra=(--depth 3 --budget-ms 10 --ignore-play-profile) ;;
  hinata2hg_d0) base=11170001; hg=1; extra=(--depth 0 --budget-ms 0 --ignore-play-profile) ;;
  *) echo "unknown cell $cell"; exit 2 ;;
esac
for gi in "$@"; do
  seed=$((base + gi))
  for arm in 0 1; do
    bash "$T/onegame2.sh" "$S/games/${cell}_${gi}_o$arm" "$seed" "$gi" "$hg" "$arm" "${extra[@]}" > /dev/null
  done
  echo "================ $cell gi=$gi seed=$seed"
  python3 "$T/showgame.py" "$S/games/${cell}_${gi}_o0/"*.json "$S/games/${cell}_${gi}_o1/"*.json | grep -E "^(OLD|NEW)" | head -16 | cut -c1-420
done

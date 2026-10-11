#!/usr/bin/env bash
# log_pairs.sh <listfile> <outdir-name> : each line "<lever A|B> <cell> <gi> <seed>"; logs the lever's
# ON and OFF arms of that game (A: base vs aoff; B: bon vs base) into logs/rs_crackle/<outdir>/<cell>_<gi>_<arm>/
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8
R=$WT/logs/rs_crackle
BIN=$R/mtg_hold1
OUT=$R/$2
while read -r lever cell gi seed; do
  extra=(); c=$cell
  if [ "$cell" = 2hg ]; then extra=(MTG_START_LIFE=30 MTG_OPPONENT_HEADS=2); c=d0; fi
  if [ "$lever" = A ]; then arms="base aoff"; else arms="bon base"; fi
  line="$lever $cell gi=$gi:"
  for arm in $arms; do
    case $arm in
      aoff) ev=(MTG_HINATA_HOLD_LETHAL_BURN=0);;
      base) ev=();;
      bon)  ev=(MTG_HINATA_HOLD_NEEDS_HINATA=1);;
    esac
    t=$(bash $R/game.sh "$BIN" $c $gi $seed $OUT/${cell}_${gi}_${arm} "${ev[@]}" "${extra[@]}" | awk '{print $4}')
    line="$line $arm=$t"
  done
  echo "$line"
done < "$1"

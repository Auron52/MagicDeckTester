#!/bin/bash
# rep2.sh <bin> <tag> <deckdir-name> <seed> <gi> <depth|-> <budget> [ENV=V...]   (depth '-' = the play profile's)
D=/workspaces/MagicDeckTester/build/scratch/maze/dm2
B=$1; tag=$2; dk=$3; seed=$4; gi=$5; d=$6; b=$7; shift 7
cd /workspaces/MagicDeckTester/.claude/worktrees/maze-dm1
export MTG_RSS_CAP_GB=20
if [ "$d" = "-" ]; then DA=(); else DA=(--depth $d --ignore-play-profile); fi
env "$@" /usr/bin/time -f "wall=%e" $B "decks/$dk/$dk.cod" --profile "decks/$dk/$dk.profile.json" --seed $seed --game-index $gi --games 1 --threads 1 "${DA[@]}" --budget-ms $b ${LOGDIR:+--log-dir $LOGDIR} > $D/$tag.txt 2>&1
grep -E 'avg \(turns\)|wall=' $D/$tag.txt | tr '\n' ' ' | sed 's/\[mean[^]]*\]//'; echo " [$tag]"

#!/bin/bash
# rep.sh <bin> <tag> <deckkey:maze|martyr|gods> <depth> <budget> [extra env...]
S=/workspaces/MagicDeckTester/build/scratch/maze/dm2
B=$1; tag=$2; k=$3; d=$4; b=$5; shift 5
case $k in
 maze) deck="decks/Maze's End/Maze's End.cod"; prof="decks/Maze's End/Maze's End.profile.json"; seed=3076; gi=73;;
 martyr) deck="decks/Martyr Lifegain/Martyr Lifegain.cod"; prof="decks/Martyr Lifegain/Martyr Lifegain.profile.json"; seed=2151; gi=149;;
 gods) deck="decks/Gods/Gods.cod"; prof="decks/Gods/Gods.profile.json"; seed=2018; gi=16;;
esac
cd /workspaces/MagicDeckTester/.claude/worktrees/maze-dm1
export MTG_RSS_CAP_GB=20
env "$@" /usr/bin/time -f "wall=%e rss=%MKB" $B "$deck" --profile "$prof" --seed $seed --game-index $gi --games 1 --threads 1 --depth $d --budget-ms $b --ignore-play-profile ${LOGDIR:+--log-dir $LOGDIR} > $S/$tag.txt 2>&1
grep -E 'avg \(turns\)|wall=' $S/$tag.txt | tr '\n' ' '; echo " [$tag]"

#!/bin/bash
# r3_one.sh <fmt std|2hg> <gi> <arm band|bandtap|bandtapfix> [depth=5] [budget=20] [extra env...]: one game of the
# round-3 A/B, single-game CLI (2HG via MTG_START_LIFE / MTG_OPPONENT_HEADS), small lane, logs on the build volume.
fmt=$1; gi=$2; arm=$3; d=${4:-5}; b=${5:-20}; shift 5 2>/dev/null || shift $#
W=/workspaces/MagicDeckTester/.claude/worktrees/elves-r2; cd $W || exit 1
M=/workspaces/MagicDeckTester/build/scratch/elves-r3/ab/mtg
L=/workspaces/MagicDeckTester/build/scratch/elves-r3/single/${fmt}_${arm}_${gi}_d${d}b${b}; mkdir -p $L
E=(MTG_RSS_CAP_GB=20)
[ "$fmt" = 2hg ] && E+=(MTG_START_LIFE=30 MTG_OPPONENT_HEADS=2)
case $arm in bandtap) E+=(MTG_ELVES_TAP_ORDER=1 MTG_ELVES_TAP_WHOLEPLAN=1);; bandtapfix) E+=(MTG_ELVES_TAP_ORDER=1);; esac
X=(); [ "$d" != 5 ] || [ "$b" != 20 ] && X=(--ignore-play-profile)
env "${E[@]}" "$@" bash scripts/box_lane.sh --slots 1 -- $M decks/Elves/Elves.cod --profile decks/Elves/Elves.profile.json \
  --seed $((9610001+gi)) --game-index $gi --depth $d --budget-ms $b --games 1 --threads 1 "${X[@]}" --log-dir $L > $L/out 2> $L/err
echo "rc=$? $L"; grep -iE "avg|win turn|digest" $L/out | head -4

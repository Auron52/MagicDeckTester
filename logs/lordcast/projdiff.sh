#!/bin/bash
# projdiff.sh <deck_dir> <gi> [extra mtg args...]: d0 [proj] trace under LORD_CAST=0 and =1; prints subsets whose read differs.
D=$1; G=$2; shift 2
S=/workspaces/MagicDeckTester/build/scratch/elves-lordcast/rc; mkdir -p $S
N=$(basename "$D"); DECK=$(ls "$D"/"$N".cod "$D"/"$N".txt 2>/dev/null | head -1)
for v in 0 1; do
  MTG_SOLVE_PROJ_TRACE=${TR:-4} MTG_RSS_CAP_GB=6 MTG_ATTACK_LORD_CAST=$v ${SEL:+MTG_LORDCAST_HAND=$SEL} \
    /workspaces/MagicDeckTester/build/scratch/elves-lordcast/${BIN:-mtg.ab} "$DECK" --profile "$D/$N.profile.json" \
    --ignore-play-profile --depth 0 --budget-ms 0 --games 1 --seed $((7340000+G)) --game-index $G "$@" \
    > $S/${N}_${G}_$v.out 2> $S/${N}_${G}_$v.err
  echo "arm$v: $(grep -o 'avg (turns)   : [0-9.]*' $S/${N}_${G}_$v.out)"
done
diff <(grep '^\[proj\]' $S/${N}_${G}_0.err | cut -c1-230) <(grep '^\[proj\]' $S/${N}_${G}_1.err | cut -c1-230) | head -${LINES_MAX:-12}

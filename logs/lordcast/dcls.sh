#!/bin/bash
# dcls.sh <deck_dir> <gi>...: for each d0 game, off vs onB (amended lever) on BIN (default mtg.r3): first divergence,
# then each arm's chosen m1 cast set at that turn as READ under the lever ([proj] proj_atk / wins) vs what was dealt.
# One line per game (dcls.py). BASE default 7460000.
D=$1; shift
SC=/workspaces/MagicDeckTester/build/scratch/elves-lordcast; BIN=${BIN:-$SC/mtg.r3}; BASE=${BASE:-7460000}
N=$(basename "$D"); DECK=$(ls "$D"/"$N".cod "$D"/"$N".txt 2>/dev/null | head -1)
ON="MTG_ATTACK_LORD_CAST=1 MTG_LORDCAST_HAND=1"; OFF="MTG_ATTACK_LORD_CAST=0"
for G in "$@"; do
  O=$SC/dc/${N}_${G}; rm -rf $O; mkdir -p $O/a $O/b
  run() { env MTG_RSS_CAP_GB=2 $1 $BIN "$DECK" --profile "$D/$N.profile.json" --ignore-play-profile --depth 0 --budget-ms 0 \
          --games 1 --seed $((BASE+G)) --game-index $G "${@:2}"; }
  run "$OFF" --log-dir $O/a > /dev/null 2>&1
  run "$ON"  --log-dir $O/b > /dev/null 2>&1
  T=$(python3 logs/lordcast/dcls.py $O turn)
  [ -n "$T" ] || { echo "$N $G nodiff"; continue; }
  run "MTG_SOLVE_PROJ_TRACE=$T $ON"  > /dev/null 2> $O/pb.err
  run "MTG_SOLVE_PROJ_TRACE=$T $OFF" > /dev/null 2> $O/pa.err
  python3 logs/lordcast/dcls.py $O report "$N $G"
done

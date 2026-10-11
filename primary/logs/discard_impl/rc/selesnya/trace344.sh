#!/bin/bash
# Order / tap / discard traces for gi 344 under both arms.
R=/workspaces/MagicDeckTester
OUT=$R/logs/discard_impl/rc/selesnya
DECK=decks/SelesnyaLifegain/SelesnyaLifegain.cod
PROF=decks/SelesnyaLifegain/SelesnyaLifegain.profile.json
gi=${1:-344}
seed=$((2002 + gi))
for arm in on off; do
  case $arm in
    on) v=1 ;;
    off) v=0 ;;
  esac
  d=$OUT/tr${gi}_$arm
  mkdir -p $d
  (cd $R; MTG_SELESNYA_BUCKET_DISCARD=$v MTG_ORDER_TRACE=1 MTG_EXEC_TAP_TRACE=1 MTG_DISCARD_TRACE=1 nice -n 19 build/PGO/mtg $DECK --profile $PROF --games 1 --seed $seed --game-index $gi --depth 0 --budget-ms 0 --ignore-play-profile --threads 1 --log-dir $d > $d/stdout.txt 2>&1)
  echo "== $arm: $(wc -l < $d/stdout.txt) lines"
done

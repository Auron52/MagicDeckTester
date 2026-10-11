#!/bin/bash
# Reproduce selesnya d0 games 175 and 344 under three arms.
R=/workspaces/MagicDeckTester
OUT=$R/logs/discard_impl/rc/selesnya
DECK=decks/SelesnyaLifegain/SelesnyaLifegain.cod
PROF=decks/SelesnyaLifegain/SelesnyaLifegain.profile.json
for gi in 175 344; do
  seed=$((2002 + gi))
  for arm in on off distoff; do
    case $arm in
      on) envs="MTG_SELESNYA_BUCKET_DISCARD=1" ;;
      off) envs="MTG_SELESNYA_BUCKET_DISCARD=0" ;;
      distoff) envs="MTG_SELESNYA_BUCKET_DISCARD=1 MTG_DISCARD_DIST_MODEL=0" ;;
    esac
    d=$OUT/g${gi}_$arm
    mkdir -p $d
    (cd $R; env $envs nice -n 19 build/PGO/mtg $DECK --profile $PROF --games 1 --seed $seed --game-index $gi --depth 0 --budget-ms 0 --ignore-play-profile --threads 1 --log-dir $d > $d/stdout.txt 2>&1)
    echo "gi=$gi arm=$arm: $(grep -i -E 'win|turn' $d/stdout.txt | tail -3 | tr '\n' ' ')"
  done
done

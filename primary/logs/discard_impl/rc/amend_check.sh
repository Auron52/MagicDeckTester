#!/bin/bash
# Do the two proposed amendments rescue the games they were written for? (Release binary, HEAD + levers)
R=/workspaces/MagicDeckTester
B=$R/build/Release/mtg
run() {   # tag deck profile seed gi depth budget env...
  local tag=$1 deck=$2 prof=$3 seed=$4 gi=$5 depth=$6 bud=$7; shift 7
  local out
  out=$(cd $R; env "$@" nice -n 19 $B "$deck" --profile "$prof" --games 1 --seed $seed --game-index $gi --depth $depth --budget-ms $bud --ignore-play-profile --threads 1 2>/dev/null | grep 'avg (turns)')
  echo "$tag: $out"
}
G=decks/Giants/Giants.cod; GP=decks/Giants/Giants.profile.json
E=decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod; EP=decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json
run "giants A/B gi1694 d0  policy off      " $G $GP 222895 1694 0 0 MTG_GIANTS_BUCKET_DISCARD=0
run "giants A/B gi1694 d0  policy on       " $G $GP 222895 1694 0 0 MTG_GIANTS_BUCKET_DISCARD=1
run "giants A/B gi1694 d0  on + rock-lib   " $G $GP 222895 1694 0 0 MTG_GIANTS_BUCKET_DISCARD=1 MTG_GIANTS_ROCK_LIBRARY_LAND=1
run "giants suite gi130 d3 policy off      " $G $GP 3133 130 3 10 MTG_GIANTS_BUCKET_DISCARD=0
run "giants suite gi130 d3 policy on       " $G $GP 3133 130 3 10 MTG_GIANTS_BUCKET_DISCARD=1
run "giants suite gi130 d3 on + rock-lib   " $G $GP 3133 130 3 10 MTG_GIANTS_BUCKET_DISCARD=1 MTG_GIANTS_ROCK_LIBRARY_LAND=1
run "edf A/B gi1424 d0     policy off      " $E $EP 218625 1424 0 0 MTG_EDF_BUCKET_DISCARD=0
run "edf A/B gi1424 d0     policy on       " $E $EP 218625 1424 0 0 MTG_EDF_BUCKET_DISCARD=1
run "edf A/B gi1424 d0     on + outlet-pips" $E $EP 218625 1424 0 0 MTG_EDF_BUCKET_DISCARD=1 MTG_EDF_OUTLET_PIPS=1

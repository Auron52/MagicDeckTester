#!/bin/bash
# Counterfactual hand-offs: force the T1 plan, then the search plays on (d3/b10), both flag arms.
R=/workspaces/MagicDeckTester
O=$R/logs/discard_impl/rc/melira
for flag in 1 0; do
  for ch in "0,1,4" "0,1,4,0" "0,1,4,1" "0,1,4,2" "0,1,4,3" "0,1,4,4"; do
    tag="cf_f${flag}_$(echo $ch | tr ',' '_')"
    mkdir -p "$O/$tag"
    (cd $R; MTG_POD_BUCKET_DISCARD=$flag nice -n 19 build/PGO/mtg "decks/Melira Pod/Melira Pod.cod" \
        --profile "decks/Melira Pod/Melira Pod.profile.json" --claude-play --choices-then-auto --seed 2035 --game-index 33 \
        --depth 3 --budget-ms 10 --ignore-play-profile --threads 1 --choices "$ch" --log-dir "$O/$tag" \
        > "$O/$tag/out.txt" 2> "$O/$tag/err.txt")
    echo "flag=$flag choices=$ch -> $(grep -o '"win_turn": *[0-9-]*' $O/$tag/out.txt | head -1)  $(grep -h 'then-auto' $O/$tag/err.txt $O/$tag/out.txt | head -2 | tr '\n' ' ')"
  done
done

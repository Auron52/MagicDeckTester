#!/bin/bash
# Repro melira_regression_d3_s2002 gi=33 under several flag arms.
R=/workspaces/MagicDeckTester
O=$R/logs/discard_impl/rc/melira
run() {
  local tag=$1; shift
  mkdir -p "$O/$tag"
  (cd $R; env "$@" MTG_SHED_STATS=1 nice -n 19 build/PGO/mtg "decks/Melira Pod/Melira Pod.cod" \
      --profile "decks/Melira Pod/Melira Pod.profile.json" --games 1 --seed 2035 --game-index 33 \
      --depth 3 --budget-ms ${BUD:-10} --ignore-play-profile --threads 1 --log-dir "$O/$tag" \
      > "$O/$tag/out.txt" 2> "$O/$tag/err.txt")
  echo "== $tag"; grep -i -E "win|turn|shed" "$O/$tag/out.txt" "$O/$tag/err.txt" | head -12
}
run on  MTG_POD_BUCKET_DISCARD=1
run off MTG_POD_BUCKET_DISCARD=0
run on_nodist MTG_POD_BUCKET_DISCARD=1 MTG_DISCARD_DIST_MODEL=0
run off_nodist MTG_POD_BUCKET_DISCARD=0 MTG_DISCARD_DIST_MODEL=0

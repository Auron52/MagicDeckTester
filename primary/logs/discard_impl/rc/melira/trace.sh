#!/bin/bash
# --trace (per-candidate search estimates) + MTG_TRACE streams for both arms.
R=/workspaces/MagicDeckTester
O=$R/logs/discard_impl/rc/melira
run() {
  local tag=$1; shift
  mkdir -p "$O/$tag"
  (cd $R; env "$@" MTG_TRACE=tiescan,search,plans,landfan nice -n 19 build/PGO/mtg "decks/Melira Pod/Melira Pod.cod" \
      --profile "decks/Melira Pod/Melira Pod.profile.json" --games 1 --seed 2035 --game-index 33 \
      --depth 3 --budget-ms ${BUD:-10} --ignore-play-profile --threads 1 --trace --log-dir "$O/$tag" \
      > "$O/$tag/out.txt" 2> "$O/$tag/err.txt")
  echo "== $tag: $(grep 'avg (turns)' $O/$tag/out.txt)  stdout=$(wc -l < $O/$tag/out.txt) stderr=$(wc -l < $O/$tag/err.txt)"
}
run tr_on  MTG_POD_BUCKET_DISCARD=1
run tr_off MTG_POD_BUCKET_DISCARD=0

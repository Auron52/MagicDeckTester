#!/bin/bash
# Per-candidate root trace (MTG_TRACE_SOLVE + MTG_TRACE_SOLVE_TURN) for both arms.
R=/workspaces/MagicDeckTester
O=$R/logs/discard_impl/rc/melira
T=${TURN:-1}
run() {
  local tag=$1; shift
  mkdir -p "$O/$tag"
  (cd $R; env "$@" MTG_TRACE_SOLVE=1 MTG_TRACE_SOLVE_TURN=$T MTG_TRACE=search nice -n 19 build/PGO/mtg "decks/Melira Pod/Melira Pod.cod" \
      --profile "decks/Melira Pod/Melira Pod.profile.json" --games 1 --seed 2035 --game-index 33 \
      --depth 3 --budget-ms ${BUD:-10} --ignore-play-profile --threads 1 --log-dir "$O/$tag" \
      > "$O/$tag/out.txt" 2> "$O/$tag/err.txt")
  echo "== $tag: $(grep 'avg (turns)' $O/$tag/out.txt)  stderr=$(wc -l < $O/$tag/err.txt)"
}
run ts${T}_on  MTG_POD_BUCKET_DISCARD=1
run ts${T}_off MTG_POD_BUCKET_DISCARD=0

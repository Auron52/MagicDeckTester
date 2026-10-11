#!/bin/bash
# FS root dump + FSW node trace at turn $TURN for both arms. SIM=1 adds the rollout sim trace.
R=/workspaces/MagicDeckTester
O=$R/logs/discard_impl/rc/melira
T=${TURN:-1}
S=${SIM:-0}
run() {
  local tag=$1; shift
  mkdir -p "$O/$tag"
  (cd $R; env "$@" MTG_FS_ROOT_DUMP=$T MTG_FS_ROOT_DUMP_SIM=$S MTG_FSW_TRACE=1 MTG_FSW_TURN=$T MTG_FSW_LINE=1 MTG_TRACE=search \
      nice -n 19 build/PGO/mtg "decks/Melira Pod/Melira Pod.cod" \
      --profile "decks/Melira Pod/Melira Pod.profile.json" --games 1 --seed 2035 --game-index 33 \
      --depth 3 --budget-ms ${BUD:-10} --ignore-play-profile --threads 1 --log-dir "$O/$tag" \
      > "$O/$tag/out.txt" 2> "$O/$tag/err.txt")
  echo "== $tag: $(grep 'avg (turns)' $O/$tag/out.txt)  stderr=$(wc -l < $O/$tag/err.txt)"
}
run fs${T}s${S}_on  MTG_POD_BUCKET_DISCARD=1
run fs${T}s${S}_off MTG_POD_BUCKET_DISCARD=0

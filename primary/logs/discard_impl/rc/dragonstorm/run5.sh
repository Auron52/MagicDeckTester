#!/bin/bash
# Counterfactual levers on the flag-ON arm: does an executor ordering lever restore T5?
R=/workspaces/MagicDeckTester
O=$R/logs/discard_impl/rc/dragonstorm
run() {
  tag=$1; shift
  mkdir -p $O/cf_$tag
  (cd $R; env "$@" MTG_EXEC_TAP_TRACE=1 nice -n 19 $R/build/PGO/mtg decks/Dragonstorm/Dragonstorm.cod --profile decks/Dragonstorm/Dragonstorm.profile.json --games 1 --seed 1534 --game-index 533 --depth 0 --budget-ms 0 --ignore-play-profile --threads 1 --log-dir $O/cf_$tag > $O/cf_out_$tag.txt 2> $O/cf_err_$tag.txt)
  echo "$tag: $(grep 'avg (turns)' $O/cf_out_$tag.txt)"
}
run on_opaque     MTG_DRAGONSTORM_BUCKET_DISCARD=1 MTG_ORDER_OPAQUE=1
run on_payable    MTG_DRAGONSTORM_BUCKET_DISCARD=1 MTG_PAYABLE_ORDER=1
run on_range      MTG_DRAGONSTORM_BUCKET_DISCARD=1 MTG_ORDER_RANGE=1 MTG_ORDER_OPAQUE=1 MTG_ORDER_RANGE_PROBE=1
run off_opaque    MTG_DRAGONSTORM_BUCKET_DISCARD=0 MTG_ORDER_OPAQUE=1
run on_d3         MTG_DRAGONSTORM_BUCKET_DISCARD=1

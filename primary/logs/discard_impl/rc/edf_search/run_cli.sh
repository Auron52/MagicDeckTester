#!/bin/bash
# Same four games via the single-run CLI with --log-dir (full JSON game log per arm).
D=/workspaces/MagicDeckTester/logs/discard_impl/rc/edf_search
B=/workspaces/MagicDeckTester/build/PGO/mtg
DECK=decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod
PROF=decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json
cd /workspaces/MagicDeckTester || exit 1
run() {  # name flag gi depth budget extra...
  local name=$1 flag=$2 gi=$3 dep=$4 bud=$5; shift 5
  mkdir -p "$D/log_$name"
  MTG_EDF_BUCKET_DISCARD=$flag MTG_SHED_STATS=1 nice -n 19 "$B" "$DECK" --profile "$PROF" --games 1 --seed 217164 \
     --game-index "$gi" --depth "$dep" --budget-ms "$bud" --threads 1 --log-dir "$D/log_$name" "$@" \
     > "$D/$name.cli.out" 2> "$D/$name.cli.err"
}
run g63_off 0 63 3 10 --ignore-play-profile &
run g63_on 1 63 3 10 --ignore-play-profile &
run g163_off 0 163 5 20 &
run g163_on 1 163 5 20 &
wait
echo done

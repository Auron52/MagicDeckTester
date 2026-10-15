#!/usr/bin/env bash
# One pooled batch through the box-wide lock (build/scratch/boxlock/RULES.txt), --threads 20.
#   run_boxed.sh <bin-tag> <dir-with-manifest.json> [ENV=VAL ...]
# Output: <dir>/batch.out, <dir>/batch.err, <dir>/run.info.
# cwd + card data: BP_RUN_CWD (default: stage_data.sh's copy on the build volume, so nothing is read over 9p);
# the manifest's deck paths are absolute.
set -u
P=/workspaces/MagicDeckTester
TAG=$1; DIR=$2; shift 2
BIN=$P/build/scratch/bp_open/bin/$TAG/mtg
RUN_CWD=${BP_RUN_CWD:-$P/build/scratch/bp_open/data}
echo "queued $(date -u +%FT%TZ) bin=$BIN cwd=$RUN_CWD env=$*" > "$DIR/run.info"
cd "$RUN_CWD"
bash "$P/build/scratch/boxlock/boxrun.sh" batch env MTG_PGO=0 MTG_DUMP_WINS=1 "$@" "$BIN" --batch "$DIR/manifest.json" --threads 20 --cards-json "$RUN_CWD/src/cards/data/cards.json" > "$DIR/batch.out" 2> "$DIR/batch.err"
echo "exit $? end $(date -u +%FT%TZ)" >> "$DIR/run.info"

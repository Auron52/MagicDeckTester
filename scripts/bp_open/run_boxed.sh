#!/usr/bin/env bash
# One pooled batch through the box-wide lock (build/scratch/boxlock/RULES.txt), --threads 20.
#   run_boxed.sh <bin-tag> <dir-with-manifest.json> [ENV=VAL ...]
# Output: <dir>/batch.out, <dir>/batch.err, <dir>/run.info. cwd = the primary checkout (absolute deck paths).
set -u
P=/workspaces/MagicDeckTester
TAG=$1; DIR=$2; shift 2
BIN=$P/build/scratch/bp_open/bin/$TAG/mtg
echo "queued $(date -u +%FT%TZ) bin=$BIN env=$*" > "$DIR/run.info"
cd "$P"
bash "$P/build/scratch/boxlock/boxrun.sh" batch env MTG_PGO=0 MTG_DUMP_WINS=1 "$@" "$BIN" --batch "$DIR/manifest.json" --threads 20 > "$DIR/batch.out" 2> "$DIR/batch.err"
echo "exit $? end $(date -u +%FT%TZ)" >> "$DIR/run.info"

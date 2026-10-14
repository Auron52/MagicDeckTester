#!/usr/bin/env bash
# Run one pooled batch: run_batch.sh <bin-tag> <dir-with-manifest.json> [out-suffix] [ENV=VAL ...]
# Output: <dir>/batch<suffix>.out, <dir>/batch<suffix>.err, <dir>/run<suffix>.info. cwd = the primary checkout
# (deck paths in the manifest are absolute; nothing is written there).
set -u
P=/workspaces/MagicDeckTester
TAG=$1; DIR=$2; SUF=${3:-}; shift 3 2>/dev/null || shift $#
BIN=$P/build/scratch/bp_open/bin/$TAG/mtg
echo "start $(date -u +%FT%TZ) bin=$BIN env=$*" > "$DIR/run$SUF.info"
cd "$P"
env MTG_PGO=0 MTG_DUMP_WINS=1 "$@" nice -n 10 "$BIN" --batch "$DIR/manifest.json" --threads 6 > "$DIR/batch$SUF.out" 2> "$DIR/batch$SUF.err"
echo "exit $? end $(date -u +%FT%TZ)" >> "$DIR/run$SUF.info"

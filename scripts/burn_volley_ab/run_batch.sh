#!/bin/bash
# Run one pooled batch: run_batch.sh <binary> <dir-with-manifest.json> [ENV=VAL ...]
# Output: <dir>/batch.out, <dir>/batch.err, <dir>/run.info. cwd = this checkout (no logs are written there).
BIN=$1; DIR=$2; shift 2
WT=$(dirname "$(dirname "$(dirname "$(readlink -f "$0")")")")
echo "start $(date -u +%FT%TZ) bin=$BIN env=$*" > "$DIR/run.info"
env -C "$WT" "$@" MTG_PGO=0 MTG_DUMP_WINS=1 nice -n 10 "$BIN" --batch "$DIR/manifest.json" --threads 6 > "$DIR/batch.out" 2> "$DIR/batch.err"
echo "exit $? end $(date -u +%FT%TZ)" >> "$DIR/run.info"

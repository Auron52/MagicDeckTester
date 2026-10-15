#!/bin/bash
# One pooled batch through the box-wide lock: run_batch.sh <binary> <dir-with-manifest.json> [ENV=VAL ...]
# Output: <dir>/batch.out, <dir>/batch.err, <dir>/run.info. cwd = this checkout (cards.json is read
# relative to it); every byte written goes to <dir> (the build volume), never the checkout.
BIN=$1; DIR=$2; shift 2
WT=$(dirname "$(dirname "$(dirname "$(readlink -f "$0")")")")
echo "queued $(date -u +%FT%TZ) bin=$BIN env=$*" > "$DIR/run.info"
bash /workspaces/MagicDeckTester/build/scratch/boxlock/boxrun.sh batch \
    env -C "$WT" "$@" MTG_PGO=0 MTG_DUMP_WINS=1 "$BIN" --batch "$DIR/manifest.json" --threads 20 \
    > "$DIR/batch.out" 2> "$DIR/batch.err"
echo "exit $? end $(date -u +%FT%TZ)" >> "$DIR/run.info"

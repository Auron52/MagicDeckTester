#!/bin/bash
# The traced probe batch through the box-wide lock: run_probe.sh <binary> <dir-with-manifest.json>
# Full per-game traces land in <dir>/trace (the build volume), plus batch.out / batch.err.
BIN=$1; DIR=$2; shift 2
WT=$(dirname "$(dirname "$(dirname "$(readlink -f "$0")")")")
mkdir -p "$DIR/trace"
echo "queued $(date -u +%FT%TZ) bin=$BIN" > "$DIR/run.info"
bash /workspaces/MagicDeckTester/build/scratch/boxlock/boxrun.sh batch \
    env -C "$WT" "$@" MTG_PGO=0 MTG_DUMP_WINS=1 "$BIN" --batch "$DIR/manifest.json" --threads 20 \
    --game-trace-dir "$DIR/trace" > "$DIR/batch.out" 2> "$DIR/batch.err"
echo "exit $? end $(date -u +%FT%TZ)" >> "$DIR/run.info"

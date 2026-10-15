#!/usr/bin/env bash
# run_batch_lock2.sh BIN MANIFEST OUTDIR THREADS CARDS_JSON [extra env assignments...]
# Like run_batch_lock.sh, but with NOTHING on the 9p mount (RULES item 9): cwd = OUTDIR on the build volume
# (the batch's own logs/batch/heartbeat.txt lands there), cards.json passed explicitly (a copy on the volume),
# deck paths in the manifest pointing at a copy of the deck folder on the volume (repath.py).
BIN=$1; MAN=$2; OUT=$3; THR=$4; CARDS=$5; shift 5
mkdir -p "$OUT/wins"
cd "$OUT" || exit 9
bash /workspaces/MagicDeckTester/build/scratch/boxlock/boxrun.sh batch \
    env MTG_DUMP_WINS=1 MTG_DUMP_UNITS=1 "$@" "$BIN" --batch "$MAN" --threads "$THR" \
    --cards-json "$CARDS" --game-log-dir "$OUT/wins" \
    > "$OUT/batch.log" 2> "$OUT/batch.err"
rc=$?
echo "rc=$rc" >> "$OUT/batch.log"
tail -3 "$OUT/batch.log"
exit $rc

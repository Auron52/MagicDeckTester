#!/usr/bin/env bash
# run_batch_lock.sh BIN MANIFEST OUTDIR THREADS [extra env assignments...]
# run_batch.sh through the box-wide batch lock (build/scratch/boxlock/RULES.txt): one batch at a time box-wide,
# niced. cwd = the worktree (its cards.json == the integration tip's), per-game .wins/.units into OUTDIR/wins,
# stdout -> OUTDIR/batch.log, stderr -> OUTDIR/batch.err.
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a4d121dd522b89155
BIN=$1; MAN=$2; OUT=$3; THR=$4; shift 4
mkdir -p "$OUT/wins"
cd "$WT" || exit 9
bash /workspaces/MagicDeckTester/build/scratch/boxlock/boxrun.sh batch \
    env MTG_DUMP_WINS=1 MTG_DUMP_UNITS=1 "$@" "$BIN" --batch "$MAN" --threads "$THR" \
    --cards-json "$WT/src/cards/data/cards.json" --game-log-dir "$OUT/wins" \
    > "$OUT/batch.log" 2> "$OUT/batch.err"
rc=$?
echo "rc=$rc" >> "$OUT/batch.log"
tail -3 "$OUT/batch.log"
exit $rc

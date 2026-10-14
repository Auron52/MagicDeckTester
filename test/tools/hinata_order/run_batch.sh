#!/usr/bin/env bash
# run_batch.sh BIN MANIFEST OUTDIR THREADS [extra env assignments...]
# One pooled mtg --batch, cwd = the worktree (cards.json there == the integration tip's), per-game
# .wins/.units into OUTDIR/wins, stdout -> OUTDIR/batch.log, stderr -> OUTDIR/batch.err.
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a4d121dd522b89155
BIN=$1; MAN=$2; OUT=$3; THR=$4; shift 4
mkdir -p "$OUT/wins"
cd "$WT" || exit 9
env MTG_DUMP_WINS=1 MTG_DUMP_UNITS=1 "$@" "$BIN" --batch "$MAN" --threads "$THR" \
    --cards-json "$WT/src/cards/data/cards.json" --game-log-dir "$OUT/wins" \
    > "$OUT/batch.log" 2> "$OUT/batch.err"
rc=$?
echo "rc=$rc" >> "$OUT/batch.log"
tail -3 "$OUT/batch.log"
exit $rc

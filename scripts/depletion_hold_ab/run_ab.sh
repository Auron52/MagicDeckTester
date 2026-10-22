#!/usr/bin/env bash
# The MTG_PAY_DEPLETION_HOLD cross-deck A/B: ONE pooled batch, both arms, every deck holding a depletion
# land, paired on held-out seeds (make_manifest.py), through the box lock at 20 threads; then the paired
# report (report.py). The binary is THIS tree's build/Release/mtg -- one binary for both arms, so the
# core-s / units ratios compare like with like. Build it with ./build.sh first (under the lock).
#   usage: scripts/depletion_hold_ab/run_ab.sh [scale]     (scale 1 = ~65-75 min at 20 threads)
# Scratch (manifest, per-game .wins, batch.log) goes on the build volume; the report is copied to logs/.
set -u
WT=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
P=$(git -C "$WT" worktree list --porcelain | awk '/^worktree /{print $2; exit}')   # the primary checkout
OUT="$P/build/scratch/depletion_hold_ab/$(date -u +%Y%m%dT%H%M%SZ)"
LOCK="$P/build/scratch/vampires/BOX.lock"
mkdir -p "$OUT" "$WT/logs/depletion_hold_ab"
python3 -I "$WT/scripts/depletion_hold_ab/make_manifest.py" "$OUT/manifest.json" "${1:-1}" | tee "$OUT/run.info"
echo "src $(git -C "$WT" rev-parse HEAD:src) queued $(date -u +%FT%TZ)" >> "$OUT/run.info"
flock "$LOCK" env -C "$WT" MTG_SLOW_GAME_MS=5000 MTG_MAX_GAME_WALL_SEC=120 \
    build/Release/mtg --batch "$OUT/manifest.json" --threads 20 --game-log-dir "$OUT/wins" \
    > "$OUT/batch.log" 2> "$OUT/batch.err"
echo "batch exit $? end $(date -u +%FT%TZ)" >> "$OUT/run.info"
python3 -I "$WT/scripts/depletion_hold_ab/report.py" "$OUT" | tee "$OUT/report.txt"
cp "$OUT/report.txt" "$OUT/run.info" "$WT/logs/depletion_hold_ab/"
echo "report: $WT/logs/depletion_hold_ab/report.txt  (scratch: $OUT)"

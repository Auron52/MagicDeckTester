#!/usr/bin/env bash
# After the mulligan generation releases the box: rebuild Release on the site-9 tree, run the gates,
# and REGENERATE the ground truth that the cherry-pick deliberately left stale.
#
# WHY IT WAITS. `mullgen.sh run` spawns FRESH mtg-analyze invocations for its two post-generation
# A/Bs. Rebuilding build/Release under it would hand those A/Bs a different binary than the
# generation they are judging -- the A/B would be measuring the rebuild, not the table.
#
# WHY GT IS REGENERATED RATHER THAN MERGED. 4cd08304 carried site 9 forward but pinned BOTH GT halves
# (regression_gt.txt AND gt_logs/*.wins) to HEAD's values, because the original's numbers came from
# its own binary five src commits ago. GT is a measurement; the only honest way to update it is to
# re-run it under the binary that ships. See CLAUDE.md's gt-rebaseline-rebase rule.
#
# DOES NOT PUSH. This touches src/**, so the push needs a human watching CI for the Windows and
# determinism jobs. Everything here is committed LOCALLY and reported.

set -u
cd /workspaces/MagicDeckTester2
OUT=logs/site9_gates
mkdir -p "$OUT"
LOG=$OUT/gates.log
log() { echo "[$(date -u '+%m-%d %H:%M:%S')] $*" | tee -a "$LOG"; }

WAIT_PIDS=${WAIT_PIDS:-}
log "=== site-9 post-gen gates START (HEAD $(git rev-parse --short HEAD)) ==="

for p in $WAIT_PIDS; do
    if kill -0 "$p" 2>/dev/null; then
        log "waiting for pid $p (the generation + its A/Bs) to exit"
        while kill -0 "$p" 2>/dev/null; do sleep 120; done
        log "  pid $p exited"
    fi
done
sleep 30

# ---- 1. Rebuild Release ------------------------------------------------------------------------
log "--- ./build.sh (Release) ---"
./build.sh >> "$OUT/build.log" 2>&1
brc=$?
log "build rc=$brc  $(ls -la build/Release/mtg 2>/dev/null | awk '{print $5, $6, $7, $8}')"
[ $brc -eq 0 ] || { log "ABORT: build failed, see $OUT/build.log"; exit 1; }

# ---- 2. Cheap gates first -- they fail fast and cost minutes, not hours -------------------------
log "--- unit ---"
build/Release/mtg-test >> "$OUT/unit.log" 2>&1
log "unit rc=$?  $(grep -oE '[0-9]+ passed \| [0-9]+ failed' "$OUT/unit.log" | tail -1)"

log "--- scenarios (a scenario failure aborts every agent -- gate before anything long) ---"
bash test/scenarios.sh >> "$OUT/scenarios.log" 2>&1
src_=$?
log "scenarios rc=$src_  $(grep -oE 'Scenarios: .*' "$OUT/scenarios.log" | tail -1)"
[ $src_ -eq 0 ] || { log "ABORT: scenarios failed -- do NOT rebaseline over this"; exit 2; }

# ---- 3. Smoke, READ BEFORE ACCEPTING ------------------------------------------------------------
log "--- smoke (expected to differ on melira d3 s3003: GT is known-stale here) ---"
bash test/regression.sh --smoke >> "$OUT/smoke.log" 2>&1
log "smoke rc=$? (differences here are the POINT -- they are what accept promotes)"
grep -iE "MISMATCH|DIFF|FAIL|melira" "$OUT/smoke.log" | tail -20 | tee -a "$LOG"

# ---- 4. Regression, then accept ------------------------------------------------------------------
# Accept is the ONLY sanctioned way to move GT (never hand-edit, never re-run to regenerate).
log "--- regression ---"
bash test/regression.sh --regression >> "$OUT/regression.log" 2>&1
log "regression rc=$?"
log "--- per-case timings (this also reads out the UNVALIDATED fungus sizing) ---"
grep -iE "^[[:space:]]*fungus|elapsed|took|[0-9]+\.[0-9]+ ?s\b" "$OUT/regression.log" | tail -30 | tee -a "$LOG"

log "--- accept: promote smoke AND regression under the rebuilt binary ---"
bash test/regression.sh --smoke      --accept >> "$OUT/accept_smoke.log" 2>&1
log "accept smoke rc=$?"
bash test/regression.sh --regression --accept >> "$OUT/accept_regression.log" 2>&1
log "accept regression rc=$?"

# ---- 5. The split-halves check -- mandatory after ANY rebase that touched GT --------------------
python3 test/check_gt_logs.py >> "$OUT/check_gt.log" 2>&1
grc=$?
log "check_gt_logs rc=$grc"
tail -15 "$OUT/check_gt.log" | tee -a "$LOG"
[ $grc -eq 0 ] || log "WARNING: check_gt_logs FAILED -- the two GT halves disagree. Do not push."

# ---- 6. Commit locally. No push: src/** needs a CI watch. ---------------------------------------
git add test/regression_gt.txt test/gt_logs 2>/dev/null
if git diff --cached --quiet; then
    log "no GT movement -- site 9 left every baseline identical"
else
    git commit -q -m "test: rebaseline GT under the site-9 binary

4cd08304 carried site 9 forward with BOTH GT halves pinned to HEAD, because the
original's numbers were produced by its own binary five src commits earlier.
This is the regeneration that commit called for, taken under the rebuilt
Release binary via --accept (never hand-edited).

check_gt_logs.py run afterwards, which is mandatory after any rebase touching
GT: conflicted files take the side you picked while non-conflicting ones replay
yours, so regression_gt.txt and gt_logs/*.wins can otherwise end up holding
different runs.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
    log "committed LOCALLY: $(git log --oneline -1)"
fi

log "=== GATES COMPLETE -- NOT PUSHED (src/** needs a CI watch for Windows + determinism) ==="
log "to push:  git push origin HEAD:phase-1-2-deck-analyzer && gh run watch"

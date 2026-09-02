#!/usr/bin/env bash
# Overnight chain (queued 2026-09-02, user asleep):
#   1. wait for the in-flight 2HG batch to finish  (generation must run ALONE -- CLAUDE.md's
#      strictly-serial generation rule; a contended gen measures a run nobody would ever do)
#   2. regenerate + validate + adopt the Dragons exhaustive mulligan profile
#   3. only if the gate PASSED: rebaseline ground truth for the dragons cases, all three tiers
#
# Waiting is on the driver log's own RC= marker, never on `pgrep -f` -- a pgrep wait-loop matches
# its own command line and fires immediately (recorded lesson).
set -uo pipefail
cd "$(dirname "$0")/.."
LOG=logs/dragons_chain.log
say(){ echo "[chain $(date -u +%H:%M:%S)] $*" | tee -a "$LOG"; }

say "=== queued: wait for 2HG -> Dragons mullgen -> GT accept ==="

# ---- 1. wait for the 2HG batch ------------------------------------------------------------
while ! grep -q "^RC=" logs/mw_2hg/driver.log 2>/dev/null; do sleep 30; done
say "2HG batch finished: $(grep '^RC=' logs/mw_2hg/driver.log)"
# nothing else may be on the box for the generation
while ps -eo comm | grep -qx mtg; do sleep 30; done
say "box is clear -- starting the Dragons generation alone"

# ---- 2. regenerate + validate + adopt -----------------------------------------------------
# `run` resumes from the intact raw sidecar (size-7 data is sound; only the bottoming sub-tables
# were starved by the feed_sub bug, fixed in 8592cb18), so this should be a pure deficit fill.
# It then runs the keep and CONFOUNDED bottoming A/Bs and adopts or quarantines on the verdict.
say "mullgen: bash scripts/mullgen.sh run decks/Dragons complete"
bash scripts/mullgen.sh run decks/Dragons complete >> "$LOG" 2>&1
MRC=$?
say "mullgen exit rc=$MRC"

PROF="decks/Dragons/Dragons.keepmodel.exhaustive.profile.json"
if [ "$MRC" -ne 0 ] || [ ! -e "$PROF" ]; then
  say "GATE DID NOT PASS (rc=$MRC, profile present=$([ -e "$PROF" ] && echo yes || echo no))."
  say "NOT touching ground truth -- CLAUDE.md: never rebaseline over a failing gate."
  say "See logs/Dragons_mullgen/VALIDATION.txt and logs/Dragons_mullgen/gen.log"
  exit 1
fi
say "profile is LIVE -> $PROF"

# ---- 3. rebaseline GT for the dragons cases only ------------------------------------------
# Adoption changes this deck's play, so its committed fingerprints are stale by construction. A
# per-deck accept rewrites only the dragons keys; every other deck's GT is left untouched.
for MODE in --smoke "" --overnight; do
  LABEL=${MODE:---regression}
  say "regression $LABEL --deck=dragons (run)"
  bash test/regression.sh $MODE --deck=dragons >> "$LOG" 2>&1
  say "regression $LABEL rc=$? -- accepting"
  bash test/regression.sh $MODE --deck=dragons --accept >> "$LOG" 2>&1
  say "accept $LABEL rc=$?"
done

say "check_gt_logs (aggregate vs per-game halves must agree)"
python3 test/check_gt_logs.py >> "$LOG" 2>&1
say "check_gt_logs rc=$?"
say "=== CHAIN COMPLETE ==="

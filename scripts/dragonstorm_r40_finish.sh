#!/usr/bin/env bash
# End-of-gen driver for the Dragonstorm R=40 exhaustive mulligan run: WAIT for the gen to finish, ADOPT
# the profile (gzip profile + raw as deck-folder siblings), then run the two ADOPTION A/Bs and report.
#   1) KEEP A/B            exhaustive keep vs static (bottoming held to lookahead both arms)
#   2) CONFOUNDED BOTTOM   blind exhaustive vs lookahead bottoming with the peek nullified
#                          (MTG_CONFOUND_BOTTOM=1 both arms -- the correct blind>=lookahead gate)
# Reconstruction A/Bs (lower R / adaptive bottoming) are a SEPARATE manual follow-up (see
# docs/design/mulligan-reconstruct-lower-r.md). This driver does NOT commit or rebaseline GT -- that
# stays a reviewed step for the user after reading the reports.
#
# Detached-safe: launch with `nohup bash scripts/dragonstorm_r40_finish.sh &`; it polls until the gen's
# DONE marker appears (or the gen dies incomplete, in which case it skips and says so).
set -u
cd /workspaces/MagicDeckTester2

HASH=e566eda
GENDIR=logs/Dragonstorm_gen
GENLOG=$GENDIR/full_R40_$HASH.log
PROF=$GENDIR/full_R40_$HASH.profile.json
RAW=$GENDIR/full_R40_$HASH.raw.json
GEN_SEED=20000001                      # unique to this gen's cmdline -> robust liveness check
DONE_MARK="exhaustive keep policy written"

DECK=decks/Dragonstorm/Dragonstorm.cod
BASE=decks/Dragonstorm/Dragonstorm.profile.json
SIB=decks/Dragonstorm/Dragonstorm.keepmodel.exhaustive     # + .profile.json.gz / .raw.json.gz
EXH=$SIB.profile.json.gz

DRIVELOG=$GENDIR/finish_driver.log
exec >> "$DRIVELOG" 2>&1
stamp(){ date -u +%Y-%m-%dT%H:%M:%SZ; }
echo "=================================================================="
echo "=== finish driver started $(stamp) -- waiting for gen DONE marker"

# ---- 1. WAIT for the gen ------------------------------------------------------------------------
while true; do
  if grep -q "$DONE_MARK" "$GENLOG" 2>/dev/null; then echo "gen DONE marker seen $(stamp)"; break; fi
  if ! pgrep -f "mtg-analyze.*seed $GEN_SEED" >/dev/null 2>&1; then
    sleep 5   # race: the marker may land in the log just as the process exits
    if grep -q "$DONE_MARK" "$GENLOG" 2>/dev/null; then echo "gen DONE marker seen (post-exit) $(stamp)"; break; fi
    echo "!! gen process gone WITHOUT the DONE marker $(stamp) -- gen is INCOMPLETE."
    echo "   Adoption + tests SKIPPED. Resume the gen (re-run $GENDIR/launch_full_R40_$HASH.cmd),"
    echo "   then re-launch this driver. (A checkpointed raw exists at $RAW.)"
    exit 1
  fi
  sleep 60
done

[ -s "$PROF" ] || { echo "!! profile missing/empty: $PROF -- abort"; exit 1; }
[ -s "$RAW"  ] || { echo "!! raw missing/empty: $RAW -- abort"; exit 1; }

# ---- 2. ADOPT (gzip profile + raw into the deck folder; raw-artifact policy = commit the .gz) -----
echo "=== ADOPT $(stamp) ==="
if [ -e "$EXH" ]; then cp -f "$EXH" "$EXH.bak.$(date -u +%Y%m%dT%H%M%SZ)" && echo "  backed up existing $EXH"; fi
gzip -c "$PROF" > "$EXH"                && echo "  wrote $EXH ($(du -h "$EXH" | cut -f1))"
gzip -c "$RAW"  > "$SIB.raw.json.gz"    && echo "  wrote $SIB.raw.json.gz ($(du -h "$SIB.raw.json.gz" | cut -f1))"

# ---- 3. ADOPTION TESTS -------------------------------------------------------------------------
echo "=== ADOPTION TEST 1/2: KEEP A/B (exhaustive keep vs static) $(stamp) ==="
KM_DECK="$DECK" KM_MODE=keep KM_STATIC="$BASE" KM_EXH_PROFILE="$EXH" \
  bash test/keepmodel_exhaustive_ab.sh || echo "!! keep A/B returned nonzero"

echo "=== ADOPTION TEST 2/2: CONFOUNDED BOTTOMING A/B (blind vs lookahead, peek nullified) $(stamp) ==="
MTG_CONFOUND_BOTTOM=1 KM_DECK="$DECK" KM_MODE=bottom KM_STATIC="$BASE" KM_EXH_PROFILE="$EXH" \
  bash test/keepmodel_exhaustive_ab.sh || echo "!! confounded bottom A/B returned nonzero"

# ---- 4. SUMMARY --------------------------------------------------------------------------------
echo "=== finish driver DONE $(stamp) ==="
echo "REPORTS:"
echo "  keep A/B (want: exh BEATS static, negative delta):"
echo "     logs/keepmodel_exh_keep_Dragonstorm/REPORT.txt"
echo "  confounded bottom A/B (want: blind >= lookahead, delta <= ~0):"
echo "     logs/keepmodel_exh_bottom_Dragonstorm/REPORT.txt   [MTG_CONFOUND_BOTTOM=1 both arms]"
echo "--- tails ---"
tail -n 6 logs/keepmodel_exh_keep_Dragonstorm/REPORT.txt 2>/dev/null
echo "  ---"
tail -n 6 logs/keepmodel_exh_bottom_Dragonstorm/REPORT.txt 2>/dev/null
echo
echo "NEXT (manual, after you review the two reports):"
echo "  * commit the adopted $EXH + $SIB.raw.json.gz"
echo "  * rebaseline GT for the mulligan shift (test/regression.sh --accept) after inspection"
echo "  * reconstruction A/Bs: SYNTH_R in {10,20} + SYNTH_BOTTOM_R=1 vs full"
echo "     (docs/design/mulligan-reconstruct-lower-r.md ; test/keep_reconstruct_ab.sh)"

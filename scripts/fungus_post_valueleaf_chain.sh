#!/usr/bin/env bash
# Unattended post-value-leaf chain for ONE deck: wait -> verify -> adoption gate -> mulligan scout
# -> gated full mulligan gen.
#
# WHY THIS EXISTS. The value-leaf run and the mulligan generation are strictly serial stages (see
# CLAUDE.md), and the agent driving them may run out of budget between the two. That would leave the
# box idle for a day. This script carries the baton unattended: it blocks until the value leaf is
# genuinely finished, waits for a HUMAN/agent adoption decision, and only then runs the mulligan
# stage -- which is also the cheapest way to produce a fresh set of pathological games to optimize
# against later (`recommend` dumps the slowest rollouts, and every gen writes <raw>.slow.log).
#
# WHAT IT DELIBERATELY DOES NOT DO: adopt the value leaf. Sidecar PRESENCE *is* adoption, and whether
# the model earns adoption is a judgment call on phase-E evidence. Automating that could silently
# ship a model that lost. The chain therefore BLOCKS on the adoption gate rather than guessing, and
# prints the exact one-line command that unblocks it.
#
# Usage:  bash scripts/fungus_post_valueleaf_chain.sh <driver_pid> [deckdir]
#   driver_pid  pid of the running valueleaf.sh; pass 0 to skip the wait (already finished).

set -u

DRIVER_PID=${1:?usage: fungus_post_valueleaf_chain.sh <driver_pid> [deckdir]}
DECKDIR=${2:-decks/Fungus}
STEM=$(basename "$DECKDIR")
VLQ=logs/vlq_$(printf '%s' "$STEM" | tr '[:upper:]' '[:lower:]')
OUT=logs/${STEM}_chain
BIN=build/Release/mtg-analyze
STAGED=logs/eval/$STEM.value.STAGED.json
LIVE=$DECKDIR/$STEM.value.json
DISABLED=$DECKDIR/$STEM.value.DISABLED.json

mkdir -p "$OUT"
LOG=$OUT/chain.log
log() { echo "[$(date -u '+%m-%d %H:%M:%S')] $*" | tee -a "$LOG"; }

DECK=""
for ext in cod txt; do [ -e "$DECKDIR/$STEM.$ext" ] && { DECK="$DECKDIR/$STEM.$ext"; break; }; done
[ -n "$DECK" ] || { log "ABORT: no decklist under $DECKDIR"; exit 1; }

log "=== post-value-leaf chain START (deck=$STEM driver=$DRIVER_PID) ==="

# ---- 1. Wait for the value-leaf driver ------------------------------------------------------
# Serial-stages rule: nothing below may share the box with the value leaf.
if [ "$DRIVER_PID" != 0 ]; then
    log "waiting for value-leaf driver pid $DRIVER_PID to exit"
    while kill -0 "$DRIVER_PID" 2>/dev/null; do sleep 120; done
    log "driver pid $DRIVER_PID has exited"
fi

# ---- 2. Verify COMPLETION by marker, never by exit code -------------------------------------
# A driver can exit non-zero having finished, or exit zero having been killed mid-phase. F_mullgen
# is the pipeline's last marker, so its presence is the only honest completion signal.
if [ ! -e "$VLQ/done/F_mullgen" ]; then
    log "STOP: $VLQ/done/F_mullgen absent -- the value-leaf run did NOT complete."
    log "      markers present: $(ls "$VLQ/done" 2>/dev/null | tr '\n' ' ')"
    log "      Resume it with:  bash scripts/valueleaf.sh run $DECKDIR"
    log "      (resume is incremental -- finished phases are skipped via their markers)"
    exit 2
fi
log "value-leaf run COMPLETE (F_mullgen present)"

# ---- 3. Adoption gate -----------------------------------------------------------------------
# The mulligan generator reads mull_gen_depth / mull_gen_budget_ms / expected_buckets out of the
# LIVE sidecar. Running before adoption inherits the play depth and measures the pre-value-leaf
# path, which is slower by a measured 1.35x-84.8x -- a projection made there is not merely noisy,
# it is wrong, and always pessimistic. So this gate is a correctness gate, not politeness.
if [ -e "$DISABLED" ] && [ ! -e "$LIVE" ]; then
    log "STOP: value leaf was REJECTED ($DISABLED present, no live sidecar)."
    log "      A mulligan gen here would inherit play settings and measure the slow path."
    log "      Nothing further to do automatically."
    exit 3
fi
if [ ! -e "$LIVE" ]; then
    log "WAITING for the adoption decision -- no $LIVE yet."
    log "  To ADOPT and release this chain:   cp $STAGED $LIVE"
    log "  To REJECT and stop this chain:     cp $STAGED $DISABLED"
    waited=0
    while [ ! -e "$LIVE" ]; do
        if [ -e "$DISABLED" ]; then
            log "STOP: $DISABLED appeared -- value leaf rejected, chain ends."
            exit 3
        fi
        sleep 300; waited=$((waited + 300))
        [ $((waited % 3600)) -eq 0 ] && log "  ... still waiting for adoption (${waited}s)"
    done
fi
log "ADOPTED: $LIVE present -- mulligan stage unblocked"
log "  contract: $(python3 -c "
import json
vp = (json.load(open('$LIVE')).get('value_play') or {})
print('mull_gen_depth=%s mull_gen_budget_ms=%s expected_buckets=%s' % (
    vp.get('mull_gen_depth'), vp.get('mull_gen_budget_ms'), vp.get('expected_buckets')))
" 2>/dev/null || echo '(unreadable)')"

# ---- 4. Mulligan SCOUT (recommend) -----------------------------------------------------------
# Bounded: discovery + exactly one rollout per cell, then project full-gen wall clock and report the
# slowest cells. Writes NO profile, so it cannot change play. Its R=1 probe chunk is reused verbatim
# by a later complete/fast gen, so this is the real gen's first slice rather than throwaway work.
# NOT via mullgen.sh: that wrapper's `run` asserts a profile was produced and recommend writes none.
[ -x "$BIN" ] || { log "ABORT: $BIN missing (do NOT rebuild while a freeze is in force)"; exit 1; }
REC=$OUT/recommend.log
log "PHASE: mulligan recommend (bounded scout, no profile written) -> $REC"
"$BIN" "$DECK" --cards-json src/cards/data/cards.json --gen-mulligan recommend > "$REC" 2>&1
rc=$?
log "recommend finished rc=$rc"
if [ $rc -ne 0 ]; then
    log "STOP: recommend failed -- see $REC"
    exit 4
fi
log "--- projection ---"
grep -E "projected|overnight target|fits|exceed|probe chunk|slowest" "$REC" | tee -a "$LOG"

# ---- 5. Gated full generation ----------------------------------------------------------------
# Parse recommend's own verdict. Unparseable => STOP, which is the safe direction: an unattended
# multi-hour gen must never start on a guess.
RECIPE=""
if   grep -q "COMPLETE fits an overnight run"  "$REC"; then RECIPE=complete
elif grep -q "fits overnight"                  "$REC"; then RECIPE=fast
elif grep -q "BOTH exceed overnight"           "$REC"; then
    log "STOP: both recipes exceed the overnight window."
    log "      The scout's slow-cell report is the deliverable; optimize against it before gen."
    log "      Slow-rollout dumps: $(ls "$DECKDIR"/*.slow.log 2>/dev/null | tr '\n' ' ')"
    exit 0
fi
if [ -z "$RECIPE" ]; then
    log "STOP: could not parse a verdict from $REC -- not starting a gen on a guess."
    exit 0
fi

log "PHASE: full mulligan generation, recipe=$RECIPE (self-validating; runs both A/Bs)"
bash scripts/mullgen.sh run "$DECKDIR" "$RECIPE" >> "$OUT/mullgen.log" 2>&1
rc=$?
log "mullgen run finished rc=$rc (see $OUT/mullgen.log and logs/${STEM}_mullgen/)"
[ $rc -eq 0 ] && log "=== CHAIN COMPLETE: mulligan profile generated and validated ===" \
              || log "=== CHAIN END: gen or validation failed -- profile may be quarantined ==="
log "slow-game evidence for later optimization:"
ls -la "$DECKDIR"/*.slow.log "$OUT"/*.log 2>/dev/null | tee -a "$LOG"
exit $rc

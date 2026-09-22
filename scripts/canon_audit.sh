#!/usr/bin/env bash
# THE UNCHALLENGEABLE-CANON AUDIT over every suite deck, at each deck's OWN PLAY SETTINGS.
#
# WHAT IT ANSWERS. The greedy breakpoint fallback is deleted (2026-09-17) -- there is no Solve()
# inside bp_searched_plan and no hatch back, and every greedy counter reads zero. What replaced it
# is a DEFAULT: at an un-branched slot the continuation is the value-best enumerated entry
# (MTG_BP_NESTED_CANON, MTG_BP_BASE_CANON). The doctrine permits a heuristic as a BRANCH'S DEFAULT
# and forbids it as a substitute for branching -- so that default is legitimate only while the
# alternatives are genuinely REACHABLE.
#
# Both fan-out routes (wave 0 and the wave walker) select plans with PlanOpensBreakpoint. A site
# masked ON that PlanOpensBreakpoint never marks therefore gets the default and NONE of the
# alternatives, at any budget, depth or width -- a heuristic wired as a prune, with no Solve()
# anywhere, so no greedy counter can see it. That is the shape that cost auras gi428 a turn, and
# the clause which closed it ends "If a route is ever added, ADD IT HERE TOO -- nothing enforces
# it." MTG_BP_CANON_AUDIT is that enforcement; this script runs it.
#
# READ IT AS: any nonzero UNCHALLENGEABLE is a doctrine violation, to be fixed by giving the site a
# clause (or a node) -- never by suppressing the audit.
#
# NO --depth / --budget-ms OVERRIDE, deliberately: the question is about the SHIPPED configuration,
# so each deck runs at its own value_play settings. Counters are contention-proof (no timing is
# read), so the decks run concurrently.

set -u
cd /workspaces/MagicDeckTester2

OUT=${OUT:-logs/canon_audit}
mkdir -p "$OUT"
LOG=$OUT/audit.log
log() { echo "[$(date -u '+%m-%d %H:%M:%S')] $*" | tee -a "$LOG"; }

BIN=build/Release/mtg
GAMES=${GAMES:-60}
SEED=${SEED:-7700001}
THREADS=${THREADS:-2}
EXTRA=${EXTRA:-}

log "=== canon audit START (HEAD $(git rev-parse --short HEAD)) games=$GAMES seed=$SEED extra='$EXTRA' ==="

pids=()
while IFS=$'\t' read -r tag file; do
    [ -n "$tag" ] || continue
    prof="${file%.*}.profile.json"
    if [ ! -f "$file" ]; then log "SKIP $tag (no decklist)"; continue; fi
    args=("$file")
    [ -f "$prof" ] && args+=(--profile "$prof")
    args+=(--seed "$SEED" --games "$GAMES" --threads "$THREADS")
    # shellcheck disable=SC2086
    MTG_BP_CANON_AUDIT=1 MTG_BATCH_HEARTBEAT=0 $EXTRA \
        "$BIN" "${args[@]}" > "$OUT/$tag.log" 2>&1 &
    pids+=($!)
done < /tmp/decks.txt

for p in "${pids[@]}"; do wait "$p"; done
log "all decks finished"

log ""
log "=== VERDICT (nonzero UNCHALLENGEABLE = a heuristic wired as a prune) ==="
bad=0
while IFS=$'\t' read -r tag file; do
    [ -f "$OUT/$tag.log" ] || continue
    hdr=$(grep -m1 'CANON AUDIT' "$OUT/$tag.log")
    n=$(grep -c 'NO ROUTE INTO THE VARIANT MACHINERY' "$OUT/$tag.log" 2>/dev/null || true)
    [ "${n:-0}" -gt 0 ] && bad=$((bad+1))
    log "$(printf '%-16s %s' "$tag" "${hdr:-(no audit line -- deck did not run)}")"
    grep 'NO ROUTE INTO THE VARIANT MACHINERY' "$OUT/$tag.log" 2>/dev/null | sed 's/^/                 /' | tee -a "$LOG"
done < /tmp/decks.txt

log ""
if [ "$bad" -eq 0 ]; then
    log "RESULT: ZERO unchallengeable canon defaults on every deck."
else
    log "RESULT: $bad deck(s) have a site whose canon default NO RANK CAN CHALLENGE -- fix the clause."
fi
log "=== canon audit COMPLETE ==="

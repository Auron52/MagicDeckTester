#!/usr/bin/env bash
# Byte-exact regression check for the exhaustive keep/bottom generator and the offline merge tool
# (src/analyzer/ExhaustiveKeep.cpp, src/analyzer/KeepModelTrainer.cpp).
#
#   bash test/lib/keepgen_check.sh base      # capture the baseline with the CURRENT binary
#   ...change code, ./build.sh...
#   bash test/lib/keepgen_check.sh mine      # re-run and byte-diff every artifact against base
#
# WHY. The regression suite drives `mtg`, not `mtg-analyze`, so it cannot see a change to profile
# GENERATION at all. A broken generator ships a subtly-worse mulligan policy that only shows up days
# later as a drifted win turn. This runs the generator over a small deterministic configuration and
# compares every byte it produces.
#
# THREE THINGS THAT SILENTLY INVALIDATE A COMPARISON, all handled here:
#   1. --seed is MANDATORY. Unseeded, the analyzer randomizes meta.seed_base per run, so the rollout
#      data differs every time and the diff shows churn that means nothing.
#   2. meta.engine_fp must be normalized away. It is a build-time hash over the ENGINE SOURCE, so it
#      moves on any edit -- including a pure code move that changes no behaviour.
#   3. The raw path is echoed into the report, so it is rewritten to <D> before comparing -- else
#      every report "differs" on the filename alone.
#   4. Each run gets its OWN raw path. Sharing one path silently couples the runs: the generator's
#      RESUME-CONTINUATION logic reads an existing out_raw as a checkpoint, so a later run can
#      inherit an earlier one's cells (and the first run of a fresh invocation sees no file at all,
#      while the second invocation does -- which made this harness nondeterministic until it was
#      caught by running it twice against one build).
#
# Deck folder: runs against a COPY under $SCRATCH, because the keep-model path writes its output next
# to the deck. Never point this at decks/ directly.
#
# NOT covered: MTG_KEEP_TRACE's journal/resume paths (they need an interrupted run) and MTG_LOG_HAND
# (it scans up to 200k seeds silently -- minutes, and not diffable in reasonable time).
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 1

tag=${1:?usage: keepgen_check.sh <tag>   (use tag \"base\" to capture the baseline)}
BIN=${MTG_ANALYZE_BIN:-./build/Release/mtg-analyze}
[ -x "$BIN" ] && : || { echo "no $BIN -- run ./build.sh first"; exit 1; }

ROOT=${KEEPGEN_CHECK_DIR:-/tmp/keepgen_check}
D="$ROOT/$tag"
SCRATCH="$ROOT/deck"
rm -rf "$D"; mkdir -p "$D"
rm -rf "$SCRATCH"; mkdir -p "$SCRATCH"; cp -r decks/burn "$SCRATCH/"
DECK="$SCRATCH/burn/burn.txt"

# Strip the build-time engine fingerprint so a pure code move does not read as a behaviour change.
norm() { python3 -c '
import re,sys
s=open(sys.argv[1]).read()
open(sys.argv[2],"w").write(re.sub(r"\"engine_fp\":\"[0-9a-f]*\"","\"engine_fp\":\"<normalized>\"",s))' "$1" "$2"; }

# gen <name> <R> <seed> [extra env...] -- one generation run; keeps the raw (and profile, if written).
# R is the only size knob: max_mull is FIXED at 6 in the analyzer CLI (there is no MTG_KEEP_MAX_MULL --
# setting one only earns a "not a flag this binary reads" warning), so every run covers sizes 7..1.
gen() {
    local name=$1 R=$2 seed=$3; shift 3
    env "$@" MTG_KEEP_EXHAUSTIVE=1 MTG_KEEP_ROLLOUTS="$R" MTG_EQUIV_PROBES=4 \
        MTG_KEEP_OUT_RAW="$D/$name.raw" MTG_KEEP_OUT_PROFILE="$D/$name.profile" \
        "$BIN" "$DECK" --seed "$seed" >"$D/$name.report" 2>"$D/$name.err" \
        || { echo "FAILED: $name"; tail -5 "$D/$name.err"; exit 1; }
    norm "$D/$name.raw" "$D/$name.raw.n"
}

# --- 1-3. plain generation, and two disjoint-seed chunks -------------------------------------------
# R=1 is below the profile floor (kMinProfileR), so it exercises the REFUSAL branch; R=10 clears it
# and so exercises BuildPolicyFromTables and the profile write.
gen gen  1 4242
gen p1  10 4242
gen p2  10 9001

# --- 4. offline merge of the two chunks -----------------------------------------------------------
merge() {
    local name=$1; shift
    env "$@" MTG_KEEP_MERGE=1 MTG_MERGE_INPUTS="$D/p1.raw,$D/p2.raw" \
        MTG_MERGE_OUT_PROFILE="$D/$name.profile" MTG_MERGE_OUT_RAW="$D/$name.raw" \
        "$BIN" "$DECK" --seed 4242 >"$D/$name.report" 2>"$D/$name.err" \
        || { echo "FAILED: $name"; tail -5 "$D/$name.err"; exit 1; }
    [ -f "$D/$name.raw" ] && norm "$D/$name.raw" "$D/$name.raw.n"
}
merge merged
# --- 5. synthetic adaptive-bottom reconstruction, and 6. the offline regret simulator --------------
merge synth MTG_KEEP_SYNTH_ADAPTIVE_BOTTOM=1
merge sim   MTG_KEEP_SIM_ADAPTIVE_BOTTOM=1 MTG_KEEP_SIM_TRIALS=32

# --- 7. execution-trace generation (adds per-cell "touched" card names to the raw) -----------------
gen traced 4 4242 MTG_KEEP_TRACE=1

# --- 8. change-detection carry: re-run against a PRIOR pool ----------------------------------------
# Needs an ADAPTIVE run (0 < R_FLOOR < R), else the prior is ignored -- which would make this path
# silently test nothing, so the report is checked for the "change-detection ON" line below.
gen carry 4 4242 MTG_KEEP_R_FLOOR=2 MTG_KEEP_PRIOR_RAW="$D/p1.raw"

# --- 9. carry + execution-trace reuse (a traced prior plus a declared changed-card set) ------------
gen carry_traced 4 4242 MTG_KEEP_R_FLOOR=2 MTG_KEEP_PRIOR_RAW="$D/traced.raw" \
                        MTG_KEEP_CHANGED_CARDS="Lightning Bolt,Goblin Guide"

# --- 10. cross-run prune set: emit one from the merge, then consume it -----------------------------
merge pruneemit MTG_KEEP_PRUNE_EMIT="$D/prune.json"
if [ -s "$D/prune.json" ]; then
    gen prune 4 4242 MTG_KEEP_PRUNE_SET="$D/prune.json"
else
    echo "NOTE: the merge emitted no prune set at this size, so the consume path is not covered"
fi

# The reports echo absolute paths; make two tags comparable.
sed -i "s#$D#<D>#g; s#$SCRATCH#<S>#g" "$D"/*.report "$D"/*.err 2>/dev/null
# WALL-CLOCK NOISE. Everything dropped below is sampled by elapsed time, so it differs run to run on the
# SAME binary (proven by running this twice against one build). Drop it before comparing; every semantic
# line (a PRIOR-RAW refusal, a PRUNE-SET carry count, a fingerprint mismatch, the adaptive wave summary)
# is left intact, so a real change still shows.
#   * stderr progress lines -- the PERCENTAGE is time-sampled, so the "(N/M tasks, R rollouts)" counts at
#     that instant move even when the run is byte-identical. Match on the shape, not the phase name: the
#     phases are floor-pass / refine-wave-<n> / full-pass and a new one must not silently escape this.
#   * the report's gen-time projection and slowest-rollout table -- pure ms measurements and their
#     derived hour estimates, whose ORDER also changes when two cells time within noise of each other.
sed -i -E '/[0-9]+% +\([0-9]+\/[0-9]+ tasks/d' "$D"/*.err 2>/dev/null
sed -i -E '/^  floor pass: [0-9]+s @ /d; /^  projected (COMPLETE|FAST)/d; /^  overnight target:/d; /^ +[0-9]+ms  size[0-9]+ (play|draw)/d' "$D"/*.report 2>/dev/null
sed -i -E 's#[0-9]+/s#<rate>/s#g; s#\b[0-9]+s\b#<t>s#g' "$D"/*.err "$D"/*.report 2>/dev/null

# Guard against a path that silently does nothing: assert the opt-in ones actually engaged.
engaged() { grep -q "$2" "$D/$1.err" "$D/$1.report" 2>/dev/null \
    && echo "  [ok] $1: $3" || { echo "  [!!] $1 did NOT engage ($3) -- this path is testing nothing"; ENG=1; }; }
ENG=0
echo "path engagement:"
engaged carry        "change-detection ON"  "prior pool loaded"
engaged carry_traced "EXECUTION-TRACE"      "trace-based cell reuse"
engaged traced       "traced"               "execution trace recorded"
engaged sim          "ADAPTIVE-BOTTOM REGRET SIM" "regret simulator ran"
engaged synth        "SYNTH-ABOT"           "adaptive-bottom reconstruction ran"
engaged prune        "PRUNE-SET: carried"   "cross-run prune set consumed"
[ "$ENG" -ne 0 ] && echo "  (engagement is part of the check: an opt-in path that is entered and skipped proves nothing)"

if [ "$tag" = base ]; then
    echo "baseline captured in $D ($(ls "$D" | wc -l) artifacts)"
    exit 0
fi

ok=0; n=0
for f in $(cd "$ROOT/base" && ls); do
    case "$f" in *.raw) continue ;; esac          # the normalized .raw.n is what we compare
    n=$((n + 1))
    if cmp -s "$ROOT/base/$f" "$D/$f"; then printf '  %-24s identical\n' "$f"
    else printf '  %-24s DIFFERS\n' "$f"; ok=1; fi
done
echo "$n artifacts compared"
exit $ok

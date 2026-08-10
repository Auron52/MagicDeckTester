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
#   5. MTG_KEEP_REFS_OFFSET=0 is PINNED. At the shipped default (2) the freeze shrink target `vg` is
#      re-derived on a TIMING-triggered schedule (when the producer observes the completed level
#      advance), so a cell sitting on the freeze threshold can stop at R=7 in one run and R=10 in the
#      next -- at a fixed seed, on one binary. That is accepted in production (rolling vg makes freeze
#      decisions more accurate; docs/design/continuous-only-keepgen.md) but it is fatal to a BYTE-EXACT
#      diff: measured on burn R=10, one size-7 cell flips ~1 run in 3, which reads as a code regression.
#      offset=0 pins vg at the floor, which is deterministic by construction, so a DIFFERS here means a
#      real behaviour change. (Everything else about the run is unchanged by the pin.)
#
# Deck folder: runs against a COPY under $SCRATCH, because the keep-model path writes its output next
# to the deck. Never point this at decks/ directly.
#
# TWO KINDS OF CHECK, both required:
#   * CROSS-TAG byte diff -- every artifact of `mine` against the same artifact of `base`. Catches any
#     behaviour change in a refactor.
#   * INVARIANTS asserted WITHIN one tag -- the generator's own claims about its reuse paths (probe
#     carry and crash-resume are advertised as producing byte-identical output). A cross-tag diff
#     cannot see these: both tags would be equally wrong. These are the reason the run costs ~7 min.
#
# NOT covered: MTG_LOG_HAND (it scans up to 200k seeds silently -- minutes, and not diffable in
# reasonable time; verified at the source level instead).
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
# Every run is ADAPTIVE/continuous -- that is the generator's only execution path (the uniform path was
# deleted: docs/design/keepgen-no-off-switches.md), so a uniform invocation here would test a shape the
# binary can no longer produce. R>=2 is a hard requirement for the same reason.
gen() {
    local name=$1 R=$2 seed=$3; shift 3
    env "$@" MTG_KEEP_EXHAUSTIVE=1 MTG_KEEP_ROLLOUTS="$R" MTG_EQUIV_PROBES=4 \
        MTG_KEEP_REFS_OFFSET=0 \
        MTG_KEEP_OUT_RAW="$D/$name.raw" MTG_KEEP_OUT_PROFILE="$D/$name.profile" \
        "$BIN" "$DECK" --seed "$seed" >"$D/$name.report" 2>"$D/$name.err" \
        || { echo "FAILED: $name"; tail -5 "$D/$name.err"; exit 1; }
    norm "$D/$name.raw" "$D/$name.raw.n"
}

# --- 1-3. plain generation, and two disjoint-seed chunks -------------------------------------------
# R=3 is below the profile floor (kMinProfileR), so it exercises the REFUSAL branch; R=10 clears it
# and so exercises BuildPolicyFromTables and the profile write. (R=1 used to serve the refusal case,
# but a cap below 2 cannot be adaptive and is now rejected outright.)
gen gen  3 4242
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
# The report is checked for the "change-detection ON" line below, so a silently-skipped carry fails.
gen carry 4 4242 MTG_KEEP_PRIOR_RAW="$D/p1.raw"

# --- 9. carry + execution-trace reuse (a traced prior plus a declared changed-card set) ------------
gen carry_traced 4 4242 MTG_KEEP_PRIOR_RAW="$D/traced.raw" \
                        MTG_KEEP_CHANGED_CARDS="Lightning Bolt,Goblin Guide"

# --- 10. cross-run prune set: emit one from the merge, then consume it -----------------------------
merge pruneemit MTG_KEEP_PRUNE_EMIT="$D/prune.json"
if [ -s "$D/prune.json" ]; then
    gen prune 4 4242 MTG_KEEP_PRUNE_SET="$D/prune.json"
else
    echo "NOTE: the merge emitted no prune set at this size, so the consume path is not covered"
fi

# --- 11-13. PROBE CARRY: a recommend probe's r=0 slice must BE this gen's r=0 slice ----------------
# --gen-mulligan recommend rolls at the deck's value_play depth (6 for burn), while the
# MTG_KEEP_EXHAUSTIVE env path defaults to MTG_EQUIV_DEPTH=5 -- so the env arms are PINNED to 6 here.
# Without that pin the two arms roll different games and the invariant fails for a reason that has
# nothing to do with the carry. Do not "simplify" it away: the play_digest gate does NOT catch the
# mismatch (measured -- docs/design/rollout-config-digest-depth-blindness.md), which is exactly why
# this check pins the depth by hand instead of trusting the gate.
env MTG_EQUIV_PROBES=4 MTG_KEEP_REFS_OFFSET=0 MTG_KEEP_OUT_RAW="$D/probe.raw" "$BIN" "$DECK" --seed 4242 \
    --gen-mulligan recommend >"$D/probe.report" 2>"$D/probe.err" \
    || { echo "FAILED: probe"; tail -5 "$D/probe.err"; exit 1; }
norm "$D/probe.raw.probe" "$D/probe.probe.n"
gen d6plain 4 4242 MTG_EQUIV_DEPTH=6
cp "$D/probe.raw.probe" "$D/d6carry.raw.probe"
gen d6carry 4 4242 MTG_EQUIV_DEPTH=6   # probe carry is unconditional -- no flag to enable

# --- 14-15. JOURNAL + byte-identical RESUME -------------------------------------------------------
# The generator's crash-safety claim: an interrupted run, restarted with the same command, resumes
# from its journal and lands on the SAME raw as an uninterrupted run.
#
# This deliberately hard-kills its OWN scratch child a few seconds in. That is the mechanism under
# test (an interrupted run is the only way to reach the resume path), NOT a result-producing run
# being cancelled -- the repo rule about never killing a run is about the latter.
#
# The interruption POINT is wall-clock dependent, so resume.err/.report are excluded from the
# cross-tag diff below (NOCMP). The asserted artifact is not: whatever the resumed run inherits, its
# final raw must equal the uninterrupted one's.
cont() { env MTG_KEEP_EXHAUSTIVE=1 MTG_KEEP_ROLLOUTS=4 MTG_KEEP_REFS_OFFSET=0 \
    MTG_EQUIV_PROBES=4 MTG_KEEP_OUT_RAW="$D/$1.raw" MTG_KEEP_OUT_PROFILE="$D/$1.profile" \
    "$BIN" "$DECK" --seed 4242 >"$D/$1.report" 2>>"$D/$1.err"; }
: >"$D/cont.err";   cont cont   || { echo "FAILED: cont"; exit 1; }
norm "$D/cont.raw" "$D/cont.raw.n"
: >"$D/resume.err"; cont resume & KPID=$!
# Interrupt only ONCE THE JOURNAL HAS RECORDS. A fixed delay does not work: the first ~7s is
# equivalence discovery, and a kill during it leaves nothing to resume from -- the restart then runs
# from scratch, the invariant below still passes, and the path silently tests nothing. (That is not
# hypothetical: an 8s fixed delay did exactly this, and only the engagement check caught it.)
for _ in $(seq 60); do [ -s "$D/resume.raw.journal" ] && break; sleep 1; done
sleep 3                           # let a few more cells commit, so the resume has real work to skip
pkill -9 -P "$KPID" 2>/dev/null   # `cont x &` may run in a subshell, so kill the child too...
kill  -9 "$KPID"    2>/dev/null   # ...and the subshell/binary itself, whichever $! turned out to be
wait "$KPID" 2>/dev/null
for _ in 1 2 3 4 5; do pgrep -f "MTG_KEEP_OUT_RAW=$D/resume.raw" >/dev/null || break; sleep 1; done
cont resume || { echo "FAILED: resume"; tail -5 "$D/resume.err"; exit 1; }
norm "$D/resume.raw" "$D/resume.raw.n"

# The reports echo absolute paths; make two tags comparable.
sed -i "s#$D#<D>#g; s#$SCRATCH#<S>#g" "$D"/*.report "$D"/*.err 2>/dev/null
# WALL-CLOCK NOISE. Everything dropped below is sampled by elapsed time, so it differs run to run on the
# SAME binary (proven by running this twice against one build). Drop it before comparing; every semantic
# line (a PRIOR-RAW refusal, a PRUNE-SET carry count, a fingerprint mismatch, the adaptive wave summary)
# is left intact, so a real change still shows.
#   * stderr progress lines -- the PERCENTAGE is time-sampled, so the "(N/M tasks|rollouts)" counts at
#     that instant move even when the run is byte-identical. Match on the shape, not the phase name, so a
#     new phase cannot silently escape this. Covers discovery, the continuous "rollouts fed" heartbeat and
#     the top-N slow dumps that now ride along with it.
#   * the report's gen-time projection and slowest-rollout tables -- pure ms measurements and their
#     derived hour estimates, whose ORDER also changes when two cells time within noise of each other.
#     (The tables print at several points now, so the ms-line pattern is matched anywhere.)
sed -i -E '/[0-9]+% +\([0-9]+\/[0-9]+ (tasks|rollouts)/d; /continuous: [0-9]+ rollouts fed/d; /^ +slowest rollouts /d; /^ +[0-9]+ms  /d' "$D"/*.err 2>/dev/null
sed -i -E '/^  floor pass: [0-9]+s @ /d; /^  projected (COMPLETE|FAST)/d; /^  overnight target:/d; /^ +slowest rollouts /d; /^ +[0-9]+ms  /d' "$D"/*.report 2>/dev/null
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
engaged d6carry      "PROBE-CARRY: reused"  "recommend probe consumed as the r=0 slice"
engaged resume       "RESUME(journal)"      "interrupted run resumed from its journal"
[ "$ENG" -ne 0 ] && echo "  (engagement is part of the check: an opt-in path that is entered and skipped proves nothing)"

# The generator's OWN invariants -- asserted within this tag, because a cross-tag diff cannot see them
# (a refactor that broke both arms equally would still compare identical).
invariant() { cmp -s "$D/$1" "$D/$2" \
    && echo "  [ok] $3" || { echo "  [!!] $3 -- VIOLATED ($1 != $2)"; INV=1; }; }
INV=0
echo "generator invariants:"
invariant d6carry.raw.n d6plain.raw.n "probe carry is byte-identical to rolling r=0 fresh"
invariant resume.raw.n  cont.raw.n    "a killed+resumed run lands on the uninterrupted run's raw"

if [ "$tag" = base ]; then
    echo "baseline captured in $D ($(ls "$D" | wc -l) artifacts)"
    exit $((ENG != 0 || INV != 0))
fi

# resume.* records how far the interrupted attempt got before the kill -- wall-clock dependent by
# construction, so it is asserted by the invariant above and NOT byte-compared across tags.
# probe.raw.journal appends per-cell records in COMPLETION order (thread timing), so its bytes differ
# between two runs of the SAME binary even though the record SET is identical -- byte-comparing it
# across tags is a coin-flip false positive (it passed once by luck). Its semantics are covered by the
# invariants: d6carry proves the probe chunk it anchors carries byte-identically, and resume proves the
# journal-fold itself is order-independent.
NOCMP=" resume.err resume.report probe.raw.journal "
ok=$((ENG != 0 || INV != 0)); n=0
for f in $(cd "$ROOT/base" && ls); do
    case "$f" in *.raw | *.raw.probe) continue ;; esac   # the normalized .raw.n / .probe.n is what we compare
    case "$NOCMP" in *" $f "*) continue ;; esac
    n=$((n + 1))
    if cmp -s "$ROOT/base/$f" "$D/$f"; then printf '  %-24s identical\n' "$f"
    else printf '  %-24s DIFFERS\n' "$f"; ok=1; fi
done
echo "$n artifacts compared"
exit $ok

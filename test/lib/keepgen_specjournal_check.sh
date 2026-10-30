#!/usr/bin/env bash
# Resume check for the keep generator's ROLLOUT SIDE JOURNAL (src/analyzer/RolloutSideJournal.h,
# ExhaustiveKeep.cpp "ROLLOUT SIDE JOURNAL").
#
#   ./build.sh && bash test/lib/keepgen_specjournal_check.sh
#
# THE INVARIANT (USER 2026-10-29): after a pause/kill, the only rollouts a resumed generation may play
# again are the ones that were mid-play when it stopped. So every interrupted run here must (a) land on
# the UNINTERRUPTED run's raw byte for byte, and (b) actually consume what the killed run finished --
# "served > 0" and fewer games played than a control that resumes the same state with the side journal
# switched off. (a) without (b) would pass on a side journal that silently does nothing.
#
# The interruption is COUNT-keyed (MTG_KEEP_DIAG_KILL_AFTER), not timed: "spec:N" signals the process
# right after the N-th speculative sub rollout is parked in the sub memo -- i.e. mid-speculation, the exact
# state the Hinata2 pause of 2026-10-29 threw away -- and "roll:N:9" SIGKILLs it after N games: every
# record is written before run_one returns, so even an uncatchable kill must lose nothing finished
# (only a MACHINE crash is bounded by the fsync cadence, which a test cannot simulate). Killing its OWN
# scratch child is the mechanism under test, not a result-producing run being cancelled.
#
# MTG_KEEP_REFS_OFFSET=0 is pinned for the same reason test/lib/keepgen_check.sh pins it (the rolling
# freeze target is re-derived on a timing-triggered schedule, which a byte diff cannot tolerate).
# MTG_KEEP_ADAPTIVE_BOTTOM=1 is what turns the adaptive sub-refine -- and with it the speculative sub
# waves -- on for the env-driven generator.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 1

BIN=${MTG_ANALYZE_BIN:-./build/Release/mtg-analyze}
[ -x "$BIN" ] || { echo "no $BIN -- run ./build.sh first"; exit 1; }
ROOT=${SPECJ_CHECK_DIR:-build/scratch/keepgen_specjournal}
R=${SPECJ_R:-10}
SEED=${SPECJ_SEED:-4242}
NSPEC=${SPECJ_KILL_SPEC:-40}     # speculative sub rollouts before the SIGTERM
NROLL=${SPECJ_KILL_ROLL:-600}    # games before the SIGKILL

rm -rf "$ROOT"; mkdir -p "$ROOT/deck"; cp -r decks/burn "$ROOT/deck/"
DECK="$ROOT/deck/burn/burn.txt"
FAIL=0
ok()  { echo "  [ok] $*"; }
bad() { echo "  [!!] $*"; FAIL=1; }

norm() { python3 -c '
import re,sys
s=open(sys.argv[1]).read()
open(sys.argv[2],"w").write(re.sub(r"\"engine_fp\":\"[0-9a-f]*\"","\"engine_fp\":\"<normalized>\"",s))' "$1" "$2"; }

# gen <dir> [env...] -- one generation into <dir>/k.raw; stdout/stderr APPEND to <dir>/report, <dir>/err.
gen() {
    local d=$1; shift
    mkdir -p "$d"
    env "$@" MTG_KEEP_EXHAUSTIVE=1 MTG_KEEP_ROLLOUTS="$R" MTG_EQUIV_PROBES=4 MTG_KEEP_REFS_OFFSET=0 \
        MTG_KEEP_ADAPTIVE_BOTTOM=1 MTG_KEEP_OUT_RAW="$d/k.raw" MTG_KEEP_OUT_PROFILE="$d/k.profile" \
        "$BIN" "$DECK" --seed "$SEED" >>"$d/report" 2>>"$d/err"
}
played() { grep -oE '\] spec-journal: [0-9]+ rollouts played live' "$1/err" | tail -1 | grep -oE '[0-9]+' | head -1; }
served() { grep -oE 'live, [0-9]+ served from the side journal' "$1/err" | tail -1 | grep -oE '[0-9]+' | head -1; }
specrecs() { [ -e "$1/k.raw.journal.spec" ] && echo $(( $(wc -l <"$1/k.raw.journal.spec") - 1 )) || echo 0; }

# same_as_ref <dir> <label>: the finished raw and profile equal the uninterrupted run's.
same_as_ref() {
    local d=$1 what=$2
    [ -e "$d/k.raw" ] || { bad "$what: no final raw (see $d/err)"; return; }
    norm "$d/k.raw" "$d/k.raw.n"
    # The profile exists only at R >= the profile floor (10); below it both runs write none.
    profeq() { if [ -e "$ROOT/ref1/k.profile" ] || [ -e "$1/k.profile" ]; then cmp -s "$1/k.profile" "$ROOT/ref1/k.profile"; fi; }
    if cmp -s "$d/k.raw.n" "$ROOT/ref1/k.raw.n" && profeq "$d"
    then ok "$what: raw + profile byte-identical to the uninterrupted run"
    else bad "$what: raw/profile DIFFER from the uninterrupted run"; fi
    [ -e "$d/k.raw.journal.spec" ] && bad "$what: side journal left behind by a FINISHED run" || true
}

# interrupt <dir> <kill-spec> <expected rc> -- run until the diag kill fires; assert it really did.
interrupt() {
    local d=$1 spec=$2 want=$3; shift 3
    gen "$d" "$@" MTG_KEEP_DIAG_KILL_AFTER="$spec"; local rc=$?
    if ! grep -q "DIAG KILL" "$d/err"; then
        bad "$d: the kill ($spec) never fired (rc $rc) -- the run finished first; lower SPECJ_KILL_*"
        return 1
    fi
    [ "$rc" -eq "$want" ] && ok "$(basename "$d"): killed by $spec, exit $rc" \
                          || bad "$(basename "$d"): killed by $spec but exit $rc (want $want)"
    local n; n=$(specrecs "$d")
    [ "$n" -gt 0 ] && ok "$(basename "$d"): side journal holds $n completed-but-uncommitted rollouts" \
                   || bad "$(basename "$d"): side journal EMPTY after the kill"
    return 0
}

echo "== reference runs"
gen "$ROOT/ref1" || { bad "ref1 failed"; tail -5 "$ROOT/ref1/err"; exit 1; }
gen "$ROOT/ref2" || { bad "ref2 failed"; exit 1; }
norm "$ROOT/ref1/k.raw" "$ROOT/ref1/k.raw.n"
norm "$ROOT/ref2/k.raw" "$ROOT/ref2/k.raw.n"
if cmp -s "$ROOT/ref1/k.raw.n" "$ROOT/ref2/k.raw.n"; then ok "two uninterrupted runs agree (the precondition)"
else bad "two uninterrupted runs DIFFER -- this configuration is not deterministic, nothing below can be judged"; exit 1; fi
grep -q "subspec=\|sub-spec hit" "$ROOT/ref1/err" && ok "speculative sub waves engaged" \
    || echo "  (note: no sub-spec read-out in ref1/err)"
[ "$(served "$ROOT/ref1")" = 0 ] && ok "a fresh run serves nothing from a side journal" || bad "fresh run served $(served "$ROOT/ref1")"

echo "== MTG_KEEP_SPEC_JOURNAL=0 restores the old behaviour"
gen "$ROOT/off" MTG_KEEP_SPEC_JOURNAL=0 || bad "off failed"
same_as_ref "$ROOT/off" "spec journal OFF"

echo "== SIGTERM (a pause) mid-speculation, resumed"
if interrupt "$ROOT/A" "spec:$NSPEC" 143; then
    cp -r "$ROOT/A" "$ROOT/A_off"
    gen "$ROOT/A" || bad "A resume failed"
    same_as_ref "$ROOT/A" "A resumed"
    gen "$ROOT/A_off" MTG_KEEP_SPEC_JOURNAL=0 || bad "A_off resume failed"
    same_as_ref "$ROOT/A_off" "A resumed with the side journal OFF (control)"
    pa=$(played "$ROOT/A"); po=$(played "$ROOT/A_off"); sa=$(served "$ROOT/A")
    [ "${sa:-0}" -gt 0 ] && ok "A: $sa rollouts served from the side journal" || bad "A: nothing served from the side journal"
    [ "${pa:-0}" -lt "${po:-0}" ] && ok "A: played $pa games on resume vs $po with the side journal off" \
                                   || bad "A: played $pa games on resume, NOT fewer than the control's $po"
fi

echo "== the same, with every banked value re-played and checked (MTG_KEEP_SPEC_JOURNAL_VERIFY)"
if interrupt "$ROOT/V" "spec:$NSPEC" 143; then
    gen "$ROOT/V" MTG_KEEP_SPEC_JOURNAL_VERIFY=1 || bad "V resume failed"
    same_as_ref "$ROOT/V" "V resumed"
    grep -q "SPEC-JOURNAL MISMATCH" "$ROOT/V/err" && bad "V: a banked value differs from the replayed game" \
        || ok "V: $(served "$ROOT/V") banked values all equal the replayed games"
fi

echo "== SIGKILL (uncatchable), resumed"
if interrupt "$ROOT/K" "roll:$NROLL:9" 137; then
    # The kill lands right after the NROLL-th game's record was written (no compaction at this size), so
    # a zero-loss writer leaves at least NROLL records; a buffered one would come up short.
    kn=$(specrecs "$ROOT/K")
    [ "$kn" -ge "$NROLL" ] && ok "K: all $NROLL games played before the SIGKILL are in the side journal ($kn records)" \
                           || bad "K: only $kn of the $NROLL games played before the SIGKILL reached the side journal"
    gen "$ROOT/K" || bad "K resume failed"
    same_as_ref "$ROOT/K" "K resumed"
    [ "$(served "$ROOT/K")" -gt 0 ] && ok "K: $(served "$ROOT/K") served" || bad "K: nothing served after a SIGKILL"
fi

echo "== a torn last record and a garbage line"
if interrupt "$ROOT/T" "spec:$NSPEC" 143; then
    printf 'garbage line\n12 0 3 4' >>"$ROOT/T/k.raw.journal.spec"
    gen "$ROOT/T" || bad "T resume failed"
    same_as_ref "$ROOT/T" "T resumed"
    grep -q "1 unparsable line(s), 1 torn last record dropped" "$ROOT/T/err" \
        && ok "T: the torn record and the garbage line were dropped, the rest used" \
        || bad "T: the load line does not report 1 unparsable + 1 torn"
fi

echo "== a side journal from ANOTHER run identity (seed_base edited)"
if interrupt "$ROOT/M" "spec:$NSPEC" 143; then
    python3 - "$ROOT/M/k.raw.journal.spec" <<'PY'
import json, sys
p = sys.argv[1]
lines = open(p).read().split("\n")
h = json.loads(lines[0]); h["specjournal"]["seed_base"] += 1
lines[0] = json.dumps(h, separators=(",", ":"))
open(p, "w").write("\n".join(lines))
PY
    gen "$ROOT/M" || bad "M resume failed"
    same_as_ref "$ROOT/M" "M resumed"
    grep -q "identity differs: seed_base" "$ROOT/M/err" && ok "M: the mismatch was named" || bad "M: mismatch not reported"
    ls "$ROOT/M"/k.raw.journal.spec.rejected.* >/dev/null 2>&1 && ok "M: the foreign side journal was PRESERVED aside" \
        || bad "M: the foreign side journal was not renamed aside"
    grep -q "RESUME(journal): reloaded" "$ROOT/M/err" && ok "M: the main journal still resumed" \
        || bad "M: a bad side journal stopped the main journal resuming"
    [ "$(served "$ROOT/M")" = 0 ] && ok "M: nothing served from the foreign file" || bad "M: served from a foreign file"
fi

echo "== the main journal gone, the side journal alone (its gate is independent)"
if interrupt "$ROOT/J" "spec:$NSPEC" 143; then
    mv "$ROOT/J/k.raw.journal" "$ROOT/J/k.raw.journal.moved"
    gen "$ROOT/J" || bad "J resume failed"
    same_as_ref "$ROOT/J" "J resumed from the side journal only"
    [ "$(served "$ROOT/J")" -gt 0 ] && ok "J: $(served "$ROOT/J") served" || bad "J: nothing served"
fi

echo "== wall-cap LEFT-OUT marks survive a kill (MTG_KEEP_ROLLOUT_WALL_SEC + forced abandonments)"
# A wall that never binds by itself (600 s) plus three rollouts forced to count as abandoned, early in the
# floor. The uninterrupted run leaves their size-7 cell-sides out; a run SIGKILLed after them and resumed
# must leave out the same ones (the {"abandon"} records) and resume the fail-out count at 3.
WALLENV=(MTG_KEEP_ROLLOUT_WALL_SEC=600 MTG_KEEP_DIAG_ABANDON=0:0:0,1:0:0,2:0:0)
gen "$ROOT/wref" "${WALLENV[@]}" || bad "wref failed"
norm "$ROOT/wref/k.raw" "$ROOT/wref/k.raw.n"
[ "$(grep -c 'ABANDONED-WALL rollout' "$ROOT/wref/err")" -eq 3 ] && ok "wref: 3 forced abandonments" \
    || bad "wref: expected 3 ABANDONED-WALL lines, got $(grep -c 'ABANDONED-WALL rollout' "$ROOT/wref/err")"
if interrupt "$ROOT/Wk" "roll:$NROLL:9" 137 "${WALLENV[@]}"; then
    nab=$(grep -c 'ABANDONED-WALL rollout' "$ROOT/Wk/err")
    gen "$ROOT/Wk" "${WALLENV[@]}" || bad "Wk resume failed"
    norm "$ROOT/Wk/k.raw" "$ROOT/Wk/k.raw.n"
    cmp -s "$ROOT/Wk/k.raw.n" "$ROOT/wref/k.raw.n" \
        && { [ ! -e "$ROOT/wref/k.profile" ] && [ ! -e "$ROOT/Wk/k.profile" ] || cmp -s "$ROOT/Wk/k.profile" "$ROOT/wref/k.profile"; } \
        && ok "Wk: resumed raw + profile identical to the uninterrupted wall run" \
        || bad "Wk: resumed raw/profile DIFFER from the uninterrupted wall run"
    if [ "$nab" -eq 3 ]; then
        grep -q "3 wall-abandoned rollout(s) replayed.*(3/" "$ROOT/Wk/err" \
            && ok "Wk: the 3 left-out marks and the fail-out count (3) were restored from the journal" \
            || bad "Wk: the resume did not replay the 3 abandonments"
    else
        echo "  (note: the kill landed after $nab of the 3 abandonments -- the count check needs all 3 first; raise SPECJ_KILL_ROLL)"
    fi
fi

echo
[ "$FAIL" -eq 0 ] && echo "SPEC-JOURNAL CHECK PASSED ($ROOT)" || echo "SPEC-JOURNAL CHECK FAILED ($ROOT)"
exit $FAIL

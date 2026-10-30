#!/usr/bin/env bash
# End-to-end check of the per-game batch RESUME (src/runner/BatchResume.h) and of the mulligan
# validation A/B that uses it (test/keepmodel_exhaustive_ab.sh).
#
#   ./build.sh && bash test/batch_resume_check.sh
#
# THE INVARIANT (USER 2026-10-29): after a pause/kill the only games replayed are the ones in progress.
# So: a batch SIGKILLed after N recorded games and re-run must (a) report every per-job line
# (played/avg/digest/units -- ms is wall time and is not compared) and every .wins file byte-identical
# to an uninterrupted run, (b) have recorded >= N games before the kill (zero loss on an uncatchable
# kill), and (c) carry them instead of replaying them. A changed MTG_* environment must start fresh and
# keep the old log aside. Then the same for a whole validation A/B round.
#
# The kill is COUNT-keyed (MTG_BATCH_DIAG_KILL_AFTER) and targets this script's own scratch child.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1
. test/lib/harness.sh
BIN=${MTG_BIN:-$(harness_bin)} || exit 1
ROOT=${BATCH_RESUME_CHECK_DIR:-build/scratch/batch_resume_check}
NKILL=${BRC_KILL:-150}
rm -rf "$ROOT"; mkdir -p "$ROOT"
FAIL=0
ok()  { echo "  [ok] $*"; }
bad() { echo "  [!!] $*"; FAIL=1; }

MF=$ROOT/manifest.json
{ echo '{ "jobs": ['
  first=1
  for s in 4004 5005 6006; do for d in 3 0; do
    [ $first -eq 1 ] && first=0 || printf ',\n'
    printf '  { "name": "burn_d%s_s%s", "deck": "decks/burn/burn.txt", "profile": "decks/burn/burn.profile.json", "games": 120, "seed": %s, "depth": %s, "budget_ms": 20, "ignore_play_profile": true }' "$d" "$s" "$s" "$d"
  done; done
  printf '\n] }\n'; } > "$MF"
KEY=$(harness_resume_key decks/burn/burn.txt decks/burn/burn.profile.json decks/burn)

# run <dir> [env...] -- the batch, its per-job lines minus the wall-clock ms field, and its .wins files
run() {
    local d=$1; shift
    mkdir -p "$d"
    env "$@" "$BIN" --batch "$MF" --threads 0 --game-log-dir "$d/wins" > "$d/batch.log" 2>> "$d/batch.err"
    local rc=$?
    grep -E '^burn_' "$d/batch.log" | sed -E 's/ ms=[0-9]+//' | sort > "$d/lines"
    return $rc
}
same() {
    if cmp -s "$1/lines" "$ROOT/ref/lines" && diff -rq "$1/wins" "$ROOT/ref/wins" >/dev/null
    then ok "$2: per-job lines and .wins files identical to the uninterrupted run"
    else bad "$2: DIFFERS from the uninterrupted run"; diff "$1/lines" "$ROOT/ref/lines" | head -5; fi
}

echo "== batch: reference"
run "$ROOT/ref" || { bad "reference batch failed"; exit 1; }
[ "$(wc -l < "$ROOT/ref/lines")" -eq 6 ] && ok "6 jobs" || bad "reference did not report 6 jobs"

echo "== batch: SIGKILL after $NKILL games, then re-run"
run "$ROOT/k" MTG_BATCH_RESUME="$ROOT/k/games.resume" MTG_BATCH_RESUME_KEY="$KEY" MTG_BATCH_DIAG_KILL_AFTER="$NKILL:9"
rc=$?
grep -q "DIAG KILL" "$ROOT/k/batch.err" && [ "$rc" -eq 137 ] && ok "killed (exit $rc)" || bad "kill did not fire as expected (exit $rc)"
n=$(( $(wc -l < "$ROOT/k/games.resume") - 1 ))
[ "$n" -ge "$NKILL" ] && ok "all $NKILL games finished before the SIGKILL are recorded ($n)" \
                      || bad "only $n of the $NKILL games finished before the SIGKILL were recorded"
run "$ROOT/k" MTG_BATCH_RESUME="$ROOT/k/games.resume" MTG_BATCH_RESUME_KEY="$KEY" || bad "resume run failed"
c=$(grep -oE 'carried [0-9]+ finished games' "$ROOT/k/batch.err" | tail -1 | grep -oE '[0-9]+')
[ "${c:-0}" -ge "$NKILL" ] && ok "re-run carried $c games instead of replaying them" || bad "re-run carried ${c:-0}"
same "$ROOT/k" "resumed batch"

echo "== batch: a different MTG_* environment must not carry"
cp -r "$ROOT/k" "$ROOT/m"
run "$ROOT/m" MTG_BATCH_RESUME="$ROOT/m/games.resume" MTG_BATCH_RESUME_KEY="$KEY" MTG_KEEP_SPEC_JOURNAL=1 || bad "m failed"
grep -q "RESUME: .* is a different batch" "$ROOT/m/batch.err" && ok "mismatch named, every game played" || bad "mismatch not detected"
ls "$ROOT/m"/games.resume.rejected.* >/dev/null 2>&1 && ok "old log PRESERVED aside" || bad "old log not kept aside"
same "$ROOT/m" "fresh batch after a mismatch"

echo "== validation A/B round (keepmodel_exhaustive_ab.sh, KM_MODE=keep): killed, then re-run"
gzip -dc decks/burn/burn.keepmodel.exhaustive.profile.json.gz > "$ROOT/exh.json"
ab() { env KM_DECK=decks/burn/burn.txt KM_MODE=keep KM_STATIC=decks/burn/burn.profile.json KM_EXH_PROFILE="$ROOT/exh.json" \
       KM_AB_SEEDS="4004 5005 6006 7007" KM_AB_GAMES=100 KM_EARLY_STOP=0 KM_OUT="$1" "${@:2}" \
       bash test/keepmodel_exhaustive_ab.sh > "$1.log" 2>&1; }
ab "$ROOT/abref"
ab "$ROOT/abk" MTG_BATCH_DIAG_KILL_AFTER="$NKILL:9"
ab "$ROOT/abk"
grep -q "carried [0-9]* finished games" "$ROOT/abk/batch.err" && ok "A/B re-run carried the killed attempt's games" \
    || bad "A/B re-run did not carry"
if cmp -s "$ROOT/abk/delta.txt" "$ROOT/abref/delta.txt" \
   && diff <(sed -n '/--- per seed ---/,/^$/p' "$ROOT/abk/REPORT.txt") <(sed -n '/--- per seed ---/,/^$/p' "$ROOT/abref/REPORT.txt") >/dev/null
then ok "A/B delta and per-seed table identical to the uninterrupted round"
else bad "A/B result differs from the uninterrupted round"; fi

echo
[ "$FAIL" -eq 0 ] && echo "BATCH RESUME CHECK PASSED ($ROOT)" || echo "BATCH RESUME CHECK FAILED ($ROOT)"
exit $FAIL

#!/usr/bin/env bash
# END-TO-END KILL TEST for `mtg --batch` per-game partial results (src/runner/BatchPartial.h).
#
#   test/batch_resume_check.sh [BIN] [OUT]
#
# 1. FULL:   the manifest uninterrupted                          -> the reference outputs.
# 2. KILLED: the same manifest, SIGKILLed once ~45% of its games have finished.
# 3. RESUME A (phase-E style): the WHOLE manifest again with --resume, into a copy of the killed dir.
#    Every .wins, every .abandoned and every result line (ms excepted) must be byte-identical to FULL,
#    and the games played must be exactly the ones the killed run had not finished.
# 4. RESUME B (matrix style): only the jobs the killed run did not report, RE-SPLIT into chunks of a
#    different size (so no job name matches a partial file), with --resume. The per-game rows of the
#    union of the killed run's completed jobs and the resumed chunks must equal FULL's.
#
# The manifest exercises: chunked jobs (game_index/seed offsets), an index-carry skip list, a tight
# absolute work ceiling (abandoned games), and a relative ceiling whose calibration games are replayed.
# MTG_ESC_SKIP_LOG=1 prints one [gdg] line per finished game, which is how games are counted; it is a
# print-only flag and is set on every run, so the job identity is the same throughout.
set -uo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
BIN=${1:-build/Release/mtg}
OUT=${2:-build/scratch/batch_resume_check}
THREADS=${THREADS:-0}
export MTG_ESC_SKIP_LOG=1
rm -rf "$OUT"; mkdir -p "$OUT"

DECK=decks/Gods/Gods.cod; PROF=decks/Gods/Gods.profile.json
DECK2=decks/Goblins/Goblins.cod; PROF2=decks/Goblins/Goblins.profile.json
[ -f "$DECK2" ] || { DECK2=$DECK; PROF2=$PROF; }

# python writes the manifests: FULL's, and the re-split builder for RESUME B.
python3 - "$OUT" "$DECK" "$PROF" "$DECK2" "$PROF2" <<'EOF'
import json, sys
out, deck, prof, deck2, prof2 = sys.argv[1:]
jobs = []
def job(name, d, p, off, n, **kw):
    j = {"name": name, "deck": d, "profile": p, "games": n, "seed": 5000 + off, "game_index": off,
         "depth": 3, "budget_ms": 10, "ignore_play_profile": True, "cell": name.rsplit("_off", 1)[0]}
    j.update(kw); jobs.append(j)
for off in (0, 24, 48):
    job("gods_d3_off%d" % off, deck, prof, off, 24)
    job("goblins_d3_off%d" % off, deck2, prof2, off, 24)
job("gods_skip_off0", deck, prof, 0, 24, skip_games=[3, 4, 17])
job("gods_tight_off0", deck, prof, 0, 24, abandon_units=4000)
for off in (0, 24):
    job("gods_rel_off%d" % off, deck, prof, off, 24, abandon_k=1.5, abandon_calib=6, abandon_floor_units=1)
json.dump({"jobs": jobs}, open(out + "/full.json", "w"), indent=1)
EOF

run() {  # run <label> <manifest> [extra args...]
    local label=$1 man=$2; shift 2
    mkdir -p "$OUT/$label/wins"
    "$BIN" --batch "$man" --threads "$THREADS" --game-log-dir "$OUT/$label/wins" "$@" \
        > "$OUT/$label/out" 2> "$OUT/$label/err"
}
games_done() { grep -c '^\[gdg\]' "$1" 2>/dev/null || true; }
lines() { grep -E '^[^ ]+: played=' "$1" | sed -E 's/ ms=[0-9]+//' | sort; }

echo "== FULL"
run full "$OUT/full.json" || { echo "FULL failed"; exit 1; }
TOTAL=$(games_done "$OUT/full/err")
echo "   $TOTAL games played"
# A run WITHOUT --resume persists nothing (opt-in, so a non-resumable caller adds no per-game writes).
if ls "$OUT/full/wins"/*.partial > /dev/null 2>&1; then echo "   FAIL: a run without --resume left partial files"; fail=1; fi

echo "== KILLED (SIGKILL at ~45% of $TOTAL games)"
mkdir -p "$OUT/killed/wins"
"$BIN" --batch "$OUT/full.json" --threads "$THREADS" --game-log-dir "$OUT/killed/wins" --resume \
    > "$OUT/killed/out" 2> "$OUT/killed/err" &
PID=$!
TARGET=$(( TOTAL * 45 / 100 ))
while kill -0 "$PID" 2>/dev/null && [ "$(games_done "$OUT/killed/err")" -lt "$TARGET" ]; do sleep 0.05; done
kill -9 "$PID" 2>/dev/null; wait "$PID" 2>/dev/null
FIN=$(games_done "$OUT/killed/err")
PERSISTED=$(cat "$OUT"/killed/wins/*.partial 2>/dev/null | grep -c '^g ' || true)
DONEJOBS=$(grep -cE '^[^ ]+: played=' "$OUT/killed/out" || true)
echo "   finished (printed) $FIN, persisted $PERSISTED, jobs reported $DONEJOBS"
cp -a "$OUT/killed" "$OUT/killed.pristine"

fail=${fail:-0}
echo "== RESUME A (whole manifest, --resume)"
cp -a "$OUT/killed.pristine" "$OUT/resumeA"
run resumeA "$OUT/full.json" --resume || { echo "RESUME A failed"; fail=1; }
REPLAYED=$(games_done "$OUT/resumeA/err")
grep '^\[batch-partial\]' "$OUT/resumeA/err" | sed 's/^/   /'
REUSED=$(sed -nE 's/.*REUSED ([0-9]+) finished.*/\1/p' "$OUT/resumeA/err")
# Each game's (job, gi): the killed run finished these, the resume played those. They must not meet:
# a finished game is never played again. (REUSED can exceed the records read, because jobs whose play
# spec, gi and seed agree -- here gods_d3_off0 and gods_skip_off0 -- are the same game.)
gdg() { sed -nE 's/^\[gdg\] job=(\S+) gi=([0-9]+) .*/\1 \2/p' "$1" | sort; }
AGAIN=$(comm -12 <(gdg "$OUT/killed.pristine/err") <(gdg "$OUT/resumeA/err") | wc -l)
NOTDONE=$(( TOTAL - FIN ))
echo "   played $REPLAYED = total $TOTAL - reused ${REUSED:-?}; games finished before the kill played again: $AGAIN"
echo "   (before this change a resume replayed every game of the $(( 10 - DONEJOBS )) unreported job(s))"
[ "$REPLAYED" -eq $(( TOTAL - ${REUSED:-0} )) ] || { echo "   FAIL: played != total - reused"; fail=1; }
[ "$AGAIN" -eq 0 ] || { echo "   FAIL: $AGAIN finished game(s) were played again"; fail=1; }
[ "$PERSISTED" -ge "$FIN" ] || { echo "   FAIL: fewer games persisted than finished"; fail=1; }
[ "$REPLAYED" -le "$NOTDONE" ] || { echo "   FAIL: played more than the $NOTDONE unfinished games"; fail=1; }
if diff <(lines "$OUT/full/out") <(lines "$OUT/resumeA/out") > "$OUT/resumeA.linediff"; then
    echo "   result lines: IDENTICAL ($(lines "$OUT/full/out" | wc -l) jobs, ms excluded)"
else echo "   FAIL: result lines differ (see $OUT/resumeA.linediff)"; fail=1; fi
if diff -r -x '*.partial' "$OUT/full/wins" "$OUT/resumeA/wins" > "$OUT/resumeA.windiff"; then
    echo "   .wins/.abandoned: IDENTICAL ($(ls "$OUT/full/wins" | wc -l) files)"
else echo "   FAIL: per-game files differ (see $OUT/resumeA.windiff)"; fail=1; fi
left=$(ls "$OUT"/resumeA/wins/*.partial 2>/dev/null | wc -l)
[ "$left" -eq 0 ] || { echo "   FAIL: $left partial file(s) left after a clean finish"; fail=1; }

echo "== RESUME B (unreported jobs only, re-split into 10-game chunks, --resume)"
cp -a "$OUT/killed.pristine" "$OUT/resumeB"
python3 - "$OUT" <<'EOF'
import json, re, sys
out = sys.argv[1]
full = json.load(open(out + "/full.json"))["jobs"]
done = {l.split(":")[0] for l in open(out + "/killed.pristine/out") if re.match(r"^\S+: played=", l)}
# The matrix driver's hand-back: a cell whose relative ceiling froze in the killed run comes back with
# that ceiling as an ABSOLUTE limit (its calibration sample cannot be re-measured from a partial cell).
ceil = {m.group(1): int(m.group(2)) for m in
        (re.search(r"\[batch\] CEILING cell=(\S+) .*? units=(\d+)", l) for l in open(out + "/killed.pristine/err")) if m}
jobs = []
for j in full:
    if j["name"] in done: continue
    base_seed = j["seed"] - j["game_index"]
    skip = set(j.get("skip_games", []))
    lo = j["game_index"]
    for off in range(lo, lo + j["games"], 10):
        n = min(10, lo + j["games"] - off)
        k = dict(j, name="%s_re%d" % (j["name"].rsplit("_off", 1)[0], off), games=n,
                 seed=base_seed + off, game_index=off)
        if k.get("cell") in ceil:
            k.update(abandon_units=ceil[k["cell"]], abandon_k=0.0, abandon_calib=0)
        sk = sorted(g for g in skip if off <= g < off + n)
        if sk: k["skip_games"] = sk
        else: k.pop("skip_games", None)
        jobs.append(k)
json.dump({"jobs": jobs}, open(out + "/resumeB.json", "w"), indent=1)
print("   %d unreported job(s) re-split into %d chunk(s)" % (len(full) - len(done), len(jobs)))
EOF
run resumeB "$OUT/resumeB.json" --resume || { echo "RESUME B failed"; fail=1; }
REPLAYED_B=$(games_done "$OUT/resumeB/err")
grep '^\[batch-partial\]' "$OUT/resumeB/err" | sed 's/^/   /'
echo "   replayed $REPLAYED_B"
python3 - "$OUT" <<'EOF' || fail=1
import glob, json, os, re, sys
out = sys.argv[1]
def rows(d, names=None):
    r = {}
    for f in glob.glob(d + "/*.wins"):
        nm = os.path.basename(f)[:-5]
        cell = re.sub(r"_(off|re)\d+$", "", nm)
        if names is not None and nm not in names: continue
        for l in open(f):
            gi, wt, dg = l.split()
            assert (cell, gi) not in r, (cell, gi)
            r[(cell, gi)] = (wt, dg)
    return r
done = {l.split(":")[0] for l in open(out + "/killed.pristine/out") if re.match(r"^\S+: played=", l)}
want = rows(out + "/full/wins")
got = rows(out + "/killed.pristine/wins", done)
got.update(rows(out + "/resumeB/wins"))
if got == want:
    print("   per-game rows (completed jobs + re-split chunks): IDENTICAL (%d games)" % len(want))
else:
    miss = set(want) ^ set(got); diff = [k for k in want if k in got and want[k] != got[k]]
    print("   FAIL: per-game rows differ: %d missing/extra, %d changed" % (len(miss), len(diff))); sys.exit(1)
EOF
[ "$fail" -eq 0 ] && echo "PASS" || echo "FAIL"
exit "$fail"

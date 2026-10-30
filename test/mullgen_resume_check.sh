#!/usr/bin/env bash
# Check of scripts/lib/mullgen_resume.sh -- the mulligan driver's restart safety (2026-10-29):
#   * PREV is the incumbent AT FREEZE (the committed profile) and is never overwritten -- the old
#     unconditional `cp -f $PROF $PREV` turned a pause between generation and validation into a
#     new-vs-new A/B;
#   * a generation that already finished is recognised (gen_finished), so the restart goes straight to
#     validation instead of re-running the generator on its own finished raw.
# Runs in a scratch git repo with fake artifacts: no binary, no games, seconds.
#
#   bash test/mullgen_resume_check.sh
set -uo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
LIB=$HERE/scripts/lib/mullgen_resume.sh
T=${MULLGEN_RESUME_CHECK_DIR:-$HERE/build/scratch/mullgen_resume_check}
rm -rf "$T"; mkdir -p "$T"
cd "$T" || exit 1
git init -q .
FAIL=0
ok()  { echo "  [ok] $*"; }
bad() { echo "  [!!] $*"; FAIL=1; }
log() { :; }
fnv() { python3 -c 'import sys
h=1469598103934665603
for c in open(sys.argv[1],"rb").read(): h=((h^c)*1099511628211)&0xFFFFFFFFFFFFFFFF
print("%016x"%h)' "$1"; }

mkdir -p decks/X logs/X_mullgen
PROF=decks/X/X.keepmodel.exhaustive.profile.json
PREV=decks/X/X.keepmodel.exhaustive.profile.PREV.json
RAW=decks/X/X.keepmodel.exhaustive.raw.json
OUT=logs/X_mullgen
echo '{"old":1}' > "$PROF.gz.src"; gzip -c "$PROF.gz.src" > "$PROF.gz"; rm -f "$PROF.gz.src"
git add "$PROF.gz"; git -c user.email=t@t -c user.name=t commit -qm incumbent
. "$LIB"

reset() { rm -f "$PREV" "$PREV".stale-* "$RAW.journal" "$RAW.journal.spec" "$OUT/gen.log"; gzip -dc "$PROF.gz" > "$PROF"; }
gensettings() { echo "=== MULLIGAN PROFILE GEN SETTINGS ===" >> "$OUT/gen.log"; }

echo "== the committed incumbent"
reset; mr_frozen_incumbent
[ -n "$INC" ] && cmp -s "$INC" "$PROF" && ok "INC = the profile committed at HEAD (unpacked from .gz)" || bad "INC not resolved"

echo "== a normal start saves PREV from the COMMITTED incumbent"
reset; mr_frozen_incumbent; mr_save_prev
cmp -s "$PREV" "$INC" && ok "PREV = incumbent" || bad "PREV wrong"

echo "== THE BUG: paused after the generator wrote the new table, before validation ended"
reset; mr_frozen_incumbent; mr_save_prev
gensettings; echo '{"new":1}' > "$PROF"
echo "[keepgen] continuous size-7 DONE: 1 rollouts" >> "$OUT/gen.log"
echo "[keepgen] GENERATION COMPLETE fnv64=$(fnv "$PROF") $PROF" >> "$OUT/gen.log"
if gen_finished; then ok "restart recognises the finished generation (-> straight to validation)"
else bad "restart would REGENERATE over a finished generation"; fi
cmp -s "$PREV" "$INC" && ok "PREV still the incumbent (not the new table)" || bad "PREV clobbered"
# and if something did call the incumbent save now, it must still not take the new table
mr_save_prev; cmp -s "$PREV" "$INC" && ok "mr_save_prev never copies the new table over PREV" || bad "mr_save_prev clobbered PREV"

echo "== the driver's own marker works too; the right table only"
reset; mr_frozen_incumbent; mr_save_prev; gensettings; echo '{"new":2}' > "$PROF"
echo "[mullgen] GENERATION COMPLETE 2026-10-29T00:00:00Z $(sha256sum < "$PROF" | cut -c1-64)" >> "$OUT/gen.log"
gen_finished && ok "[mullgen] marker accepted" || bad "[mullgen] marker rejected"
echo '{"new":3}' > "$PROF"
gen_finished && bad "a marker for a DIFFERENT table was accepted" || ok "a marker for a different table is not accepted"

echo "== after a verdict, the next run regenerates"
reset; mr_frozen_incumbent; mr_save_prev; gensettings; echo '{"new":1}' > "$PROF"
echo "[keepgen] GENERATION COMPLETE fnv64=$(fnv "$PROF") $PROF" >> "$OUT/gen.log"
echo "[mullgen] VALIDATION VERDICT rc=0 2026-10-29T00:00:00Z" >> "$OUT/gen.log"
gen_finished && bad "a validated generation was treated as unfinished-validation" || ok "verdict recorded -> regenerate"

echo "== mid-generation (journal or side journal present) is never 'finished'"
for f in "$RAW.journal" "$RAW.journal.spec"; do
    reset; mr_frozen_incumbent; mr_save_prev; gensettings; echo '{"new":1}' > "$PROF"
    echo "[keepgen] GENERATION COMPLETE fnv64=$(fnv "$PROF") $PROF" >> "$OUT/gen.log"
    : > "$f"
    gen_finished && bad "finished despite $(basename "$f")" || ok "$(basename "$f") present -> resume the generation"
done

echo "== a generation finished under the OLD driver/binary (no marker)"
reset; mr_frozen_incumbent; mr_save_prev; gensettings; echo '{"new":1}' > "$PROF"
echo "[keepgen] continuous size-7 DONE: 1 rollouts" >> "$OUT/gen.log"
gen_finished && ok "legacy: PREV=incumbent and PROF new -> finished" || bad "legacy finished generation not recognised"
rm -f "$PREV"
gen_finished && bad "legacy without PREV (an accepted run) treated as finished" || ok "legacy without PREV -> regenerate"

echo "== a stale PREV is moved aside, never overwritten"
reset; echo '{"stale":1}' > "$PREV"; mr_frozen_incumbent; mr_save_prev
cmp -s "$PREV" "$INC" && ok "PREV = incumbent" || bad "PREV wrong"
ls "$PREV".stale-* >/dev/null 2>&1 && grep -q stale "$PREV".stale-* && ok "the stale PREV is PRESERVED aside" || bad "stale PREV lost"

echo "== a first version (nothing committed)"
git rm -q --cached "$PROF.gz"; git -c user.email=t@t -c user.name=t commit -qm drop
reset; rm -f "$PROF.gz"; mr_frozen_incumbent
[ -z "$INC" ] && ok "no committed incumbent" || bad "INC should be empty"
gensettings; echo '{"first":1}' > "$PROF"
echo "[keepgen] GENERATION COMPLETE fnv64=$(fnv "$PROF") $PROF" >> "$OUT/gen.log"
gen_finished && ok "first version: finished generation recognised" || bad "first version not recognised"

echo
[ "$FAIL" -eq 0 ] && echo "MULLGEN RESUME CHECK PASSED" || echo "MULLGEN RESUME CHECK FAILED"
exit $FAIL

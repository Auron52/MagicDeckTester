# shellcheck shell=bash
# Restart safety for scripts/mullgen.sh (2026-10-29). Sourced; reads the driver's globals
# RAW PROF PREV OUT and its log(). See the "THE INCUMBENT, AND A GENERATION THAT ALREADY FINISHED" note
# in mullgen.sh. Checked by test/mullgen_resume_check.sh.

# mr_frozen_incumbent -> sets INC to the profile committed at HEAD (gz or plain), unpacked into
# $OUT/incumbent.frozen.json; INC="" when nothing is committed (a first version).
mr_frozen_incumbent(){
  INC=""
  local c
  for c in "$PROF.gz" "$PROF"; do
    if git cat-file -e "HEAD:$c" 2>/dev/null; then
      case "$c" in *.gz) git show "HEAD:$c" | gzip -dc > "$OUT/incumbent.frozen.json" ;;
                    *)    git show "HEAD:$c"            > "$OUT/incumbent.frozen.json" ;; esac \
        && [ -s "$OUT/incumbent.frozen.json" ] && INC=$OUT/incumbent.frozen.json
      return 0
    fi
  done
}

# mr_save_prev -- PREV := the incumbent at freeze, never overwriting an existing PREV (one that differs
# from the committed incumbent is moved aside). Without a committed incumbent (a local table) an
# existing PREV is kept as it is.
mr_save_prev(){
  if [ -n "$INC" ]; then
    if [ -e "$PREV" ] && ! cmp -s "$PREV" "$INC"; then
      local stale="$PREV.stale-$(date -u +%Y%m%dT%H%M%SZ)"
      mv "$PREV" "$stale"
      log "WARNING: $PREV was not the committed incumbent -- moved aside to $stale (not deleted)"
    fi
    [ -e "$PREV" ] || cp "$INC" "$PREV"
    log "regeneration: incumbent saved -> $PREV (the profile committed at $(git rev-parse --short HEAD))"
  elif [ -e "$PROF" ]; then
    if [ ! -e "$PREV" ]; then cp "$PROF" "$PREV"; log "regeneration: incumbent saved -> $PREV"
    elif cmp -s "$PREV" "$PROF"; then log "regeneration: incumbent saved -> $PREV"
    else log "regeneration: incumbent saved -> $PREV (KEPT: it differs from $PROF and nothing committed says"
         log "    which is the incumbent, so the existing PREV is not overwritten -- check it)"; fi
  fi
}

# gen_finished -> true when the LAST generation of this deck already finished and its validation has not
# reached a verdict (i.e. a pause landed between the two). Needs $INC (the committed incumbent, or empty).
#   * no journal and no side journal (nothing left to generate), and $PROF exists;
#   * in gen.log, after the last generator start, a GENERATION COMPLETE marker for EXACTLY the table now in
#     $PROF -- the generator's own "[keepgen] GENERATION COMPLETE fnv64=..." (printed after the profile is
#     written and BEFORE the journal is removed, so no kill can fall between them) or this driver's
#     "[mullgen] GENERATION COMPLETE <sha256>" -- and no "[mullgen] VALIDATION VERDICT" after it. For a
#     generation that finished under binaries/drivers older than both markers, the generator's
#     "continuous size-7 DONE" line;
#   * on a regeneration, PREV is the committed incumbent and $PROF is not (the old-driver case relies
#     on this: an accepted run removed PREV, a rejected one restored $PROF to it).
gen_finished(){
  [ ! -e "$RAW.journal" ] && [ ! -e "$RAW.journal.spec" ] && [ -e "$PROF" ] && [ -e "$OUT/gen.log" ] || return 1
  local mode
  mode=$(python3 - "$OUT/gen.log" "$(sha256sum < "$PROF" | cut -c1-64)" "$PROF" <<'GF'
import sys
h = 1469598103934665603
for c in open(sys.argv[3], "rb").read():
    h = ((h ^ c) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
fnv = "%016x" % h
lines = open(sys.argv[1], errors="replace").read().splitlines()
starts = [i for i, l in enumerate(lines) if "=== MULLIGAN PROFILE GEN SETTINGS ===" in l]
if not starts:
    print("no"); sys.exit()
tail = lines[starts[-1]:]
if any(l.startswith("[mullgen] VALIDATION VERDICT") for l in tail):
    print("no"); sys.exit()
done = [l for l in tail if l.startswith("[mullgen] GENERATION COMPLETE") or l.startswith("[keepgen] GENERATION COMPLETE")]
if done:
    ok = any((l.startswith("[mullgen]") and l.split()[-1] == sys.argv[2]) or
             (l.startswith("[keepgen]") and ("fnv64=" + fnv) in l) for l in done[-2:])
    print("marker" if ok else "no")
elif any("[keepgen] continuous size-7 DONE" in l for l in tail):
    print("legacy")
else:
    print("no")
GF
)
  case "$mode" in
    marker) [ -z "$INC" ] && return 0
            [ -e "$PREV" ] && cmp -s "$PREV" "$INC" && ! cmp -s "$PROF" "$INC" ;;
    legacy) [ -n "$INC" ] && [ -e "$PREV" ] && cmp -s "$PREV" "$INC" && ! cmp -s "$PROF" "$INC" ;;
    *)      return 1 ;;
  esac
}


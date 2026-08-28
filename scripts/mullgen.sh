#!/usr/bin/env bash
# Exhaustive mulligan profile: GENERATE **and VALIDATE**, as one indivisible operation.
#
#   bash scripts/mullgen.sh run      decks/<Deck> [complete|fast]   # generate, then validate + gate
#   bash scripts/mullgen.sh validate decks/<Deck>                   # validate an existing profile
#   bash scripts/mullgen.sh status   decks/<Deck>                   # where is it up to
#
# WHY THIS EXISTS (user, 2026-08-28). The A/B validation was already prescribed in
# .claude/skills/mulligan-profile.md and it still did not get run -- a 3.9 h StompySurprise
# generation landed a profile that was LIVE (keep is presence-gated: the file existing IS adoption)
# with zero games played against it. An instruction that depends on an agent remembering it is not a
# gate. So the gate moved into the process: you cannot generate without validating, because it is
# the same command.
#
# WHAT IT RUNS, and why the two cases differ:
#
#   FIRST VERSION (no prior profile)   -- the question is "is this policy any good at all?"
#     1. KM_MODE=keep    exhaustive keep vs the static profile (bottoming held identical)
#     2. KM_MODE=bottom + MTG_CONFOUND_BOTTOM=1   blind exhaustive bottoming vs lookahead, with the
#        library reshuffled AFTER the decision. The confound correction is not optional: the naive
#        bottoming A/B scores lookahead on the very library it peeked at, so it "wins" for free.
#
#   REGENERATION (a profile already exists) -- the question is "is the new table better than the one
#   we already ship?", which neither of the above can ask. One significant A/B, old vs new, both
#   arms in the shipping condition (KM_MODE=versus).
#
# THE BAR (user, 2026-08-28): "at all worse on average". No significance margin -- the mean delta in
# avg win turn is the verdict. A profile that is worse by any amount does not ship.
#
# ON FAILURE: the profile is QUARANTINED to <stem>.keepmodel.exhaustive.profile.DISABLED.json.
# That name is deliberate -- keep is presence-gated on the exact profile filename, so renaming it is
# what actually deactivates it (the same reason a rejected value model ships as .value.DISABLED.json).
# The artifact is preserved for inspection, just not in play. On a REGEN failure the previous profile
# is additionally RESTORED, so the deck keeps its last known-good policy instead of dropping to
# static.
set -uo pipefail
cd "$(dirname "$0")/.."

CMD=${1:?usage: mullgen.sh run|validate|status decks/<Deck> [recipe]}
DECKDIR=${2:?usage: mullgen.sh run|validate|status decks/<Deck> [recipe]}
RECIPE=${3:-complete}
DECKDIR=${DECKDIR%/}
STEM=$(basename "$DECKDIR")

DECK=""
for ext in cod txt; do [ -e "$DECKDIR/$STEM.$ext" ] && { DECK="$DECKDIR/$STEM.$ext"; break; }; done
[ -n "$DECK" ] || { echo "no $STEM.cod or $STEM.txt in $DECKDIR"; exit 1; }

BASE=$DECKDIR/$STEM.profile.json
PROF=$DECKDIR/$STEM.keepmodel.exhaustive.profile.json
RAW=$DECKDIR/$STEM.keepmodel.exhaustive.raw.json
PREV=$DECKDIR/$STEM.keepmodel.exhaustive.profile.PREV.json
DIS=$DECKDIR/$STEM.keepmodel.exhaustive.profile.DISABLED.json
OUT=logs/${STEM}_mullgen; mkdir -p "$OUT"
REPORT=$OUT/VALIDATION.txt
BIN=build/Release/mtg-analyze

# Seeds. The regen A/B is the "significant test" the accept rule names, so it gets a wider, DISJOINT
# seed set than the first-version checks -- an adoption decision should not be read off the same
# seeds twice.
FIRST_SEEDS=${MULLGEN_FIRST_SEEDS:-"4004 5005 6006 7007 8008 9009 10010 11011 12012 13013 14014 15015 16016 17017 18018 19019"}
VS_SEEDS=${MULLGEN_VS_SEEDS:-"21021 22022 23023 24024 25025 26026 27027 28028 29029 30030 31031 32032 33033 34034 35035 36036 37037 38038 39039 40040 41041 42042 43043 44044"}

stamp(){ date -u +%Y-%m-%dT%H:%M:%SZ; }
log(){ echo "$*" | tee -a "$REPORT"; }

# MINIMUM SAMPLE, enforced before anything can be quarantined. Found the hard way: a 2-seed x
# 40-game plumbing smoke test deactivated a real 3.9 h profile on +0.0375t of pure noise. The gate
# deactivates a live artifact, so it must refuse to render a verdict it has no power to support --
# an under-powered reject is not a conservative failure, it is a wrong one. Override only to make it
# STRICTER; MULLGEN_ALLOW_SMALL=1 is the deliberate escape for plumbing tests (it skips the gate,
# it does not lower it).
MIN_SEEDS=${MULLGEN_MIN_SEEDS:-12}
MIN_GAMES=${MULLGEN_MIN_GAMES:-500}
check_power(){
  local seeds="$1" n; n=$(echo $seeds | wc -w)
  local games=${KM_AB_GAMES:-1000}
  if [ "${MULLGEN_ALLOW_SMALL:-0}" = 1 ]; then
    log "NOTE: MULLGEN_ALLOW_SMALL=1 -- running $n seeds x $games games for PLUMBING ONLY; the"
    log "      accept/quarantine gate is DISABLED for this run (no verdict will be acted on)."
    return 1
  fi
  if [ "$n" -lt "$MIN_SEEDS" ] || [ "$games" -lt "$MIN_GAMES" ]; then
    log "REFUSING TO GATE: $n seeds x $games games is below the $MIN_SEEDS x $MIN_GAMES minimum."
    log "  This gate deactivates a live profile, so it will not rule on a sample this small."
    log "  Use the defaults, or set MULLGEN_ALLOW_SMALL=1 to run ungated for plumbing."
    exit 2
  fi
  return 0
}

status_report(){
  echo "deck      : $DECK"
  echo "profile   : $([ -e "$PROF" ] && echo "PRESENT (LIVE)" || echo absent)  $PROF"
  echo "quarantine: $([ -e "$DIS" ] && echo PRESENT || echo absent)  $DIS"
  echo "previous  : $([ -e "$PREV" ] && echo PRESENT || echo absent)"
  echo "journal   : $([ -e "$RAW.journal" ] && echo "PRESENT (gen incomplete/resumable)" || echo "absent (gen complete or not started)")"
  if [ -e "$REPORT" ]; then echo "--- last validation ---"; tail -20 "$REPORT"; fi
}

# run_ab <mode> <tag> <seeds> [extra env assignments...] -> echoes the mean delta (B-A), turns.
run_ab(){
  local mode="$1" tag="$2" seeds="$3"; shift 3
  local abdir=logs/keepmodel_exh_${mode}_${STEM}
  rm -f "$abdir/delta.txt"
  env "$@" \
    KM_DECK="$DECK" KM_MODE="$mode" KM_STATIC="$BASE" KM_EXH_PROFILE="$PROF" \
    KM_AB_SEEDS="$seeds" \
    bash test/keepmodel_exhaustive_ab.sh > "$OUT/ab_${tag}.log" 2>&1
  [ -s "$abdir/delta.txt" ] || { echo "ERR"; return 1; }
  cat "$abdir/delta.txt"
}

# worse <delta> -> true when the B arm is worse ON AVERAGE by any amount (the accept bar).
worse(){ awk -v d="$1" 'BEGIN{ exit !(d > 0) }'; }

quarantine(){
  local why="$1"
  mv -f "$PROF" "$DIS"
  log ""
  log "!!! VALIDATION FAILED -- $why"
  log "!!! profile QUARANTINED -> $DIS"
  log "!!! keep is presence-gated, so the deck now falls back to its static/default policy."
  if [ -e "$PREV" ]; then
    cp -f "$PREV" "$PROF"
    log "!!! previous profile RESTORED -> $PROF (deck keeps its last known-good policy)"
  fi
  log "!!! full A/B output: $OUT/"
}

validate(){
  [ -e "$PROF" ] || { echo "no profile to validate: $PROF"; exit 1; }
  [ -e "$BASE" ] || { echo "missing base/static profile: $BASE"; exit 1; }
  [ -x build/Release/mtg ] || { echo "build/Release/mtg missing -- run ./build.sh"; exit 1; }

  # Artifact check first: it is free, and a malformed table makes every game below meaningless.
  python3 - "$PROF" <<'PY' || exit 1
import json, sys
ek = (json.load(open(sys.argv[1])) or {}).get("exhaustive_keep") or {}
K, ents = len(ek.get("buckets") or []), (ek.get("entries") or [])
bad = sum(1 for e in ents if not e.get("bottom_keep") or any(len(r) != K for r in e["bottom_keep"]))
if not ents or not ek.get("bottoming_enabled") or bad:
    print(f"ARTIFACT CHECK FAILED: entries={len(ents)} bottoming_enabled="
          f"{ek.get('bottoming_enabled')} malformed_bottom_keep={bad}")
    sys.exit(1)
print(f"artifact check OK: K={K} entries={len(ents)} bottoming_enabled=True")
PY

  local gated=1
  if [ -e "$PREV" ]; then
    log "=== REGENERATION: significant A/B, old vs new ($(stamp)) deck=$STEM ==="
    log "bar: new must not be worse ON AVERAGE than old (any amount is a reject)"
    check_power "$VS_SEEDS" || gated=0
    local d; d=$(KM_EXH_A="$PREV" KM_EXH_B="$PROF" run_ab versus versus "$VS_SEEDS") || {
      log "A/B did not produce a delta -- see $OUT/ab_versus.log"
      [ "$gated" = 1 ] && { quarantine "versus A/B failed to run"; return 1; }; return 1; }
    log "new-vs-old delta: ${d}t  (negative = new wins)"
    [ "$gated" = 1 ] || { log "UNGATED run -- verdict recorded, profile left as-is."; return 0; }
    if worse "$d"; then quarantine "new profile is worse than the old by ${d}t on average"; return 1; fi
    log "ACCEPTED: new profile is not worse than the old. Keeping it."
    rm -f "$PREV"
  else
    log "=== FIRST VERSION: keep + confounded bottoming ($(stamp)) deck=$STEM ==="
    log "bar: neither check may be worse ON AVERAGE (any amount is a reject)"
    check_power "$FIRST_SEEDS" || gated=0
    local dk db
    dk=$(run_ab keep keep "$FIRST_SEEDS") || { log "keep A/B failed to run"
      [ "$gated" = 1 ] && { quarantine "keep A/B failed to run"; return 1; }; return 1; }
    log "keep (exhaustive vs static) delta: ${dk}t  (negative = exhaustive wins)"
    db=$(run_ab bottom bottom_confounded "$FIRST_SEEDS" MTG_CONFOUND_BOTTOM=1) || {
      log "confounded bottoming A/B failed to run"
      [ "$gated" = 1 ] && { quarantine "bottoming A/B failed to run"; return 1; }; return 1; }
    log "bottoming (blind vs lookahead, CONFOUNDED) delta: ${db}t  (negative = blind wins)"
    [ "$gated" = 1 ] || { log "UNGATED run -- verdicts recorded, profile left as-is."; return 0; }
    if worse "$dk"; then quarantine "exhaustive keep is worse than static by ${dk}t"; return 1; fi
    if worse "$db"; then
      # The skill's position is that a confounded-bottoming loss means "raise R / fix the heuristic",
      # not "ship bottoming off" -- and there IS no bottoming off switch, so the only lever the gate
      # has is the whole profile. Quarantining is the reversible direction; say so explicitly rather
      # than let a failing table stay live.
      quarantine "blind bottoming is worse than lookahead by ${db}t under the CONFOUNDED A/B -- per
    .claude/skills/mulligan-profile.md the response is to raise R or fix the bottoming heuristic,
    NOT to ship bottoming off (there is no off switch). Re-enable by renaming the .DISABLED.json
    back once addressed."
      return 1
    fi
    log "ACCEPTED: keep and confounded bottoming both clear the bar."
  fi
  log "=== VALIDATION PASSED ($(stamp)) -- $PROF is live ==="
}

case "$CMD" in
  status)   status_report ;;
  validate) : > "$REPORT"; validate ;;
  run)
    [ -x "$BIN" ] || { echo "$BIN missing -- run ./build.sh"; exit 1; }
    : > "$REPORT"
    # A regen must keep the incumbent to compare against -- and to restore if the new one loses.
    if [ -e "$PROF" ]; then cp -f "$PROF" "$PREV"; log "regeneration: incumbent saved -> $PREV"; fi
    log "=== GEN ($(stamp)) deck=$STEM recipe=$RECIPE ==="
    "$BIN" "$DECK" --cards-json src/cards/data/cards.json --gen-mulligan "$RECIPE" \
      >> "$OUT/gen.log" 2>&1 || { log "GENERATION FAILED -- see $OUT/gen.log"; exit 1; }
    [ -e "$PROF" ] || { log "gen produced no profile (below the R>=10 floor?) -- see $OUT/gen.log"; exit 1; }
    log "gen complete -> $PROF"
    validate ;;
  *) echo "unknown command '$CMD' (run|validate|status)"; exit 1 ;;
esac

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

# SEEDS, allocated in per-round blocks. Every check draws from its own base so no two checks -- and
# no two ROUNDS of a check -- ever share a seed: pooling rounds is only valid over disjoint games,
# and re-reading a decision off seeds it was already made on is how a tie gets confirmed by its own
# noise. keepmodel_pool_ab.py drops a duplicate loudly rather than double-counting, but the
# allocation is what makes duplicates impossible in the first place.
KEEP_BASE=4004; BOTTOM_BASE=1004004; VS_BASE=2004004; SEED_STEP=1001
# The regen A/B is sized to match the first-version validation's TOTAL weight (user: it should be
# "in the same ballpark as the lookahead bottoming and keep tests"): first version runs TWO A/Bs at
# 16 seeds x 2 arms = 64k games, so the regen A/B runs 32 seeds x 2 arms = 64k. It is the sole
# evidence for replacing a shipped table, so it cannot be the cheapest measurement in the pipeline.
KEEP_PER_ROUND=${MULLGEN_PER_ROUND:-16}
VS_PER_ROUND=${MULLGEN_VS_PER_ROUND:-32}

# ESCALATION ON A TIE. The accept bar is "at all worse on average" with NO significance margin, so a
# delta of +0.0001 rejects -- which is only sound if the delta is real. When a round lands too close
# to call, another round of FRESH seeds is run and the rounds are POOLED (user, 2026-08-28: "we might
# want to run another set of 64000 if the result is less than some low threshold. We want to be
# fairly confident in the result either way"). Applied to all three checks; in practice keep and
# confounded bottoming "win handily" (measured on StompySurprise: mean/se -46 and -16, nowhere near a
# tie), so this is really for the profile comparison, where two tables of the same deck genuinely can
# be near-identical.
#
# "Too close to call" is BOTH an absolute floor and a confidence test: a delta under TIE_ABS is too
# small to care about either way, and one under 2 standard errors is not distinguishable from zero.
MAX_ROUNDS=${MULLGEN_MAX_ROUNDS:-3}
TIE_ABS=${MULLGEN_TIE_ABS:-0.005}

# seed_block <base> <count> <round0> -- disjoint block of `count` seeds for round `round0` (0-based).
seed_block(){
  local base=$1 count=$2 round=$3 i out=""
  for ((i = 0; i < count; i++)); do out+="$(( base + (round * count + i) * SEED_STEP )) "; done
  echo "$out"
}

stamp(){ date -u +%Y-%m-%dT%H:%M:%SZ; }
log(){ echo "$*" | tee -a "$REPORT"; }
# Same, but never touches stdout: run_ab is called inside $( ), where stdout IS the return value, so
# a plain log() there gets captured into the delta instead of being printed.
rlog(){ echo "$*" >> "$REPORT"; echo "$*" >&2; }

# MINIMUM SAMPLE, enforced before anything can be quarantined. Found the hard way: a 2-seed x
# 40-game plumbing smoke test deactivated a real 3.9 h profile on +0.0375t of pure noise. The gate
# deactivates a live artifact, so it must refuse to render a verdict it has no power to support --
# an under-powered reject is not a conservative failure, it is a wrong one. Override only to make it
# STRICTER; MULLGEN_ALLOW_SMALL=1 is the deliberate escape for plumbing tests (it skips the gate,
# it does not lower it).
MIN_SEEDS=${MULLGEN_MIN_SEEDS:-12}
MIN_GAMES=${MULLGEN_MIN_GAMES:-500}
check_power(){
  local n="$1"
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

# run_ab <mode> <tag> <base> <per-round> <a-tag> <b-tag> [extra env...] -> echoes the POOLED mean
# delta (B-A) in turns, escalating on a tie. Round dirs are <abdir>_r<N>; POOLED.txt holds the final
# pooled table and is what inline_ab shows.
run_ab(){
  local mode="$1" tag="$2" base="$3" per="$4" atag="$5" btag="$6"; shift 6
  local abdir=logs/keepmodel_exh_${mode}_${STEM}
  local dirs=() r delta se
  for (( r = 0; r < MAX_ROUNDS; r++ )); do
    local rd="${abdir}_r$((r+1))"
    rm -rf "$rd"
    local seeds; seeds=$(seed_block "$base" "$per" "$r")
    env "$@" \
      KM_DECK="$DECK" KM_MODE="$mode" KM_STATIC="$BASE" KM_EXH_PROFILE="$PROF" \
      KM_AB_SEEDS="$seeds" KM_OUT="$rd" \
      bash test/keepmodel_exhaustive_ab.sh > "$OUT/ab_${tag}_r$((r+1)).log" 2>&1
    [ -s "$rd/delta.txt" ] || { echo "ERR"; return 1; }
    dirs+=("$rd")
    python3 test/keepmodel_pool_ab.py --a-tag "$atag" --b-tag "$btag" "${dirs[@]}" \
      > "${abdir}_POOLED.txt" 2>&1
    delta=$(cat "${dirs[-1]}/pooled_delta.txt" 2>/dev/null) || return 1
    se=$(cat "${dirs[-1]}/pooled_se.txt" 2>/dev/null || echo 0)
    # Resolved? |delta| must clear BOTH the absolute floor and 2 standard errors.
    if awk -v d="$delta" -v s="$se" -v t="$TIE_ABS" \
         'BEGIN{ ad = (d<0?-d:d); exit !(ad >= t && ad >= 2*s) }'; then
      break
    fi
    if [ $((r+1)) -lt "$MAX_ROUNDS" ]; then
      rlog "  round $((r+1)): delta ${delta}t (se ${se}) -- too close to call, escalating to round $((r+2))"
    else
      rlog "  round $((r+1)): delta ${delta}t (se ${se}) -- still within the tie band at MAX_ROUNDS=$MAX_ROUNDS;"
      rlog "                  ruling on the pooled mean anyway (the bar has no significance margin)."
    fi
  done
  echo "$delta"
}

# Fold an A/B's full report into VALIDATION.txt. Called for EVERY check, whatever the verdict (user,
# 2026-08-28: "either way, it should be reporting the results exhaustively so we can decide what to
# do next"). A rejected profile is precisely when the detail matters most -- the mean is the gate,
# but deciding what to DO next (raise R? fix the heuristic? one bad seed?) needs the spread.
inline_ab(){
  local mode="$1" title="$2" abdir=logs/keepmodel_exh_${1}_${STEM}
  log ""
  log "########## $title ##########"
  # The POOLED table covers every round that was run; each round's own REPORT.txt is kept beside it
  # in <abdir>_r<N>/ for anyone who wants the round-by-round view.
  if [ -e "${abdir}_POOLED.txt" ]; then
    sed 's/^/  /' "${abdir}_POOLED.txt" | tee -a "$REPORT"
  else
    log "  (no pooled report at ${abdir}_POOLED.txt -- see $OUT/ab_*.log)"
  fi
}

# worse <delta> -> true when the B arm is worse ON AVERAGE by any amount (the accept bar).
worse(){ awk -v d="$1" 'BEGIN{ exit !(d > 0) }'; }

# REGRESSION, run for VISIBILITY and never as a rejection. Adopting a mulligan profile moves the
# deck's GT, and it is EXPECTED to look like a slowdown: the standard goldfish metric is
# unconfounded, so it still rewards the lookahead bottomer's peek at the library. A blind policy
# that is genuinely better (which the confounded A/B above is what actually establishes) therefore
# scores slightly WORSE on that metric. Rejecting on it would be rejecting a profile for failing a
# test we already know is biased against it (user, 2026-08-28: "We don't reject if those succeed,
# because lookahead bottoming has clairvoyance bias").
run_regression(){
  local key
  key=$(awk -v d="$DECK" '/^[[:space:]]*\[[a-z0-9_]+\]=/ {
          line=$0; sub(/^[[:space:]]*\[/, "", line); split(line, a, "\\]=");
          if (a[2] == d) { print a[1]; exit } }' test/regression_cases.sh)
  log ""
  if [ -z "$key" ]; then
    log "--- regression: $STEM is not in test/regression_cases.sh -- skipping (nothing to move) ---"
    return 0
  fi
  log "--- regression (deck=$key), for VISIBILITY -- cannot reject ($(stamp)) ---"
  bash test/regression.sh --deck="$key" > "$OUT/regression.log" 2>&1
  local rc=$?
  grep -E '^Result:|^ALL PASS|^REGRESSION DETECTED' "$OUT/regression.log" | while read -r l; do log "  $l"; done
  grep -E '^\s+\[(searched|d0)\s*\]' "$OUT/regression.log" | while read -r l; do log "  $l"; done
  if [ "$rc" -ne 0 ]; then
    # Report the direction MEASURED, not an assumed one. The first version of this text asserted
    # that GT would look like a slowdown (the standard metric still rewards lookahead's peek, so a
    # better BLIND policy can read as a win-turn increase). On StompySurprise it was the opposite:
    # all 5 cases moved FASTER, mean -0.26t, faster=285/slower=104 -- because the keep policy
    # dominates and swamps the bottoming effect. Asserting a direction the run then contradicts is
    # how a report stops being trusted, so derive it from the audit line instead.
    local faster slower
    faster=$(grep -oE 'faster=[0-9]+' "$OUT/regression.log" | head -1 | cut -d= -f2)
    slower=$(grep -oE 'slower=[0-9]+' "$OUT/regression.log" | head -1 | cut -d= -f2)
    log "  GT moved -- EXPECTED when a mulligan profile is adopted, and NOT a rejection."
    if [ -n "${faster:-}" ] && [ -n "${slower:-}" ] && [ "$faster" -ge "$slower" ]; then
      log "  Direction: net FASTER (faster=$faster slower=$slower). The new policy is simply better"
      log "  on this metric too -- nothing to reconcile."
    else
      log "  Direction: net slower (faster=${faster:-?} slower=${slower:-?}). Expected when BOTTOMING"
      log "  dominates: the standard metric is unconfounded, so it still rewards lookahead's peek and"
      log "  a better BLIND policy reads as a win-turn increase. The confounded A/B is the verdict."
    fi
    log "  Accepting the new GT is a separate, deliberate call: bash test/regression.sh --accept"
  fi
  log "  full output: $OUT/regression.log"
  return 0
}

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
    check_power "$VS_PER_ROUND" || gated=0
    local d; d=$(KM_EXH_A="$PREV" KM_EXH_B="$PROF" run_ab versus versus "$VS_BASE" "$VS_PER_ROUND" old new) || {
      log "A/B did not produce a delta -- see $OUT/ab_versus.log"
      [ "$gated" = 1 ] && { quarantine "versus A/B failed to run"; return 1; }; return 1; }
    log "new-vs-old delta: ${d}t  (negative = new wins)"
    inline_ab versus "PROFILE COMPARISON: old table vs new (both in the shipping condition)"
    log ""
    log "########## VERDICT ##########"
    log "  new-vs-old ${d}t   $(worse "$d" && echo 'WORSE -> reject, keep the old' || echo 'ok -> adopt the new')"
    [ "$gated" = 1 ] || { log "UNGATED run -- verdict recorded, profile left as-is."; return 0; }
    if worse "$d"; then quarantine "new profile is worse than the old by ${d}t on average"; return 1; fi
    log "ACCEPTED: new profile is not worse than the old. Keeping it."
    rm -f "$PREV"
    run_regression
  else
    log "=== FIRST VERSION: keep + confounded bottoming ($(stamp)) deck=$STEM ==="
    log "bar: neither check may be worse ON AVERAGE (any amount is a reject)"
    check_power "$KEEP_PER_ROUND" || gated=0
    local dk db
    dk=$(run_ab keep keep "$KEEP_BASE" "$KEEP_PER_ROUND" static exh) || { log "keep A/B failed to run"
      [ "$gated" = 1 ] && { quarantine "keep A/B failed to run"; return 1; }; return 1; }
    log "keep (exhaustive vs static) delta: ${dk}t  (negative = exhaustive wins)"
    db=$(run_ab bottom bottom_confounded "$BOTTOM_BASE" "$KEEP_PER_ROUND" lookahead exhbottom MTG_CONFOUND_BOTTOM=1) || {
      log "confounded bottoming A/B failed to run"
      [ "$gated" = 1 ] && { quarantine "bottoming A/B failed to run"; return 1; }; return 1; }
    log "bottoming (blind vs lookahead, CONFOUNDED) delta: ${db}t  (negative = blind wins)"
    # Exhaustive detail BEFORE the gate decides, so a rejection ships with its evidence attached.
    inline_ab keep   "KEEP: exhaustive vs static (bottoming held identical)"
    inline_ab bottom "BOTTOMING: blind exhaustive vs lookahead, CONFOUNDED (peek nullified)"
    log ""
    log "########## VERDICT ##########"
    log "  keep      ${dk}t   $(worse "$dk" && echo 'WORSE -> reject' || echo 'ok')"
    log "  bottoming ${db}t   $(worse "$db" && echo 'WORSE -> reject' || echo 'ok')"
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
    run_regression
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

#!/usr/bin/env bash
# Unattended Mirrorwing mulligan generation + validation, staged for a Monday adoption decision.
#
# NOTHING HERE ADOPTS. Generation runs against a STAGED COPY of the deck folder, so every artifact
# lands in logs/mullgen_mirrorwing/staged/ and decks/Mirrorwing Dragon/ is never written to. The
# engine resolves sibling models directory-relative off the profile path, so writing the keep model
# next to the real deck WOULD arm it in play -- which is the user's call, not this script's.
#
# There is deliberately NO decision point that waits for an agent: the recipe is chosen up front
# (see below), so the only way this stops early is a crash, which STATUS records.
#
# RECIPE CHOICE (made 2026-08-16, not re-derived here): the post-mana-fix floor rate measured
# 427,067 rollouts in 2,100 s = 203/s under perf overhead, so the projection formula the engine
# itself uses (total_cells * 40 / floor_rate) gives complete ~= 22.2 h upper bound against a ~33 h
# window. `complete` (full bottoming, R40) is the definitive, adopt-able profile, so it is worth the
# window over `fast`. If it overruns, the journal makes it resumable -- nothing is lost.
set -u
cd /workspaces/MagicDeckTester

DECK="${1:-Mirrorwing Dragon}"
SRC="decks/$DECK"
STAGE="logs/mullgen_${2:-mirrorwing}"
SD="$STAGE/staged"
CJ="src/cards/data/cards.json"
STATUS="$STAGE/STATUS.txt"

mkdir -p "$SD"
say() { echo "[$(date -u '+%F %T')] $*" >> "$STATUS"; }

# --- freeze -------------------------------------------------------------------------------------
# Generation artifacts are engine-state fingerprints, so the commit and src TREE are recorded here.
# A pull during the run would change HEAD:src and invalidate a resume: DO NOT pull until this ends.
: > "$STATUS"
say "FREEZE commit=$(git rev-parse --short HEAD) src=$(git rev-parse HEAD:src)"

# The staged dir is a fully-working deck folder: decklist + play profile + value sidecar (for the
# mull_gen d2/b3 routing) + the equivalence gencache, so bucketing is not re-derived.
cp "$SRC/$DECK.cod" "$SRC/$DECK.profile.json" "$SRC/$DECK.value.json" "$SD/" \
  || { say "FATAL: staging copy failed"; exit 1; }
# The equivalence gencache is an OPTIONAL input (it only skips a re-derivation); a deck being
# generated for the first time has none, and that must not abort the run.
if [ -f "$SRC/$DECK.keepmodel.gencache.json" ]; then cp "$SRC/$DECK.keepmodel.gencache.json" "$SD/"
else say "no gencache for $DECK -- equivalence discovery will re-derive it (first generation)"; fi
say "staged deck at $SD (real deck folder is untouched)"

# --- phase 1: generation ------------------------------------------------------------------------
say "PHASE 1 generation: --gen-mulligan complete (full bottoming, R40)"
./build/Release/mtg-analyze "$SD/$DECK.cod" --cards-json "$CJ" --gen-mulligan complete \
    > "$STAGE/gen.log" 2>&1
rc=$?
say "PHASE 1 exit=$rc"
if [ $rc -ne 0 ]; then
    say "GENERATION FAILED -- see $STAGE/gen.log (journal in $SD is resumable: rerun this script)"
    exit 1
fi
ls -la "$SD" >> "$STATUS"

# --- phase 2: regret read-out (from the GENERATION LOG, not a re-analysis) ----------------------
# NEVER run a bare `mtg-analyze <deck>` here. That is the analyzer's MAIN mode: it regenerates and
# OVERWRITES <deck>.profile.json, which silently replaced the staged deck's card_scores mid-run on
# 2026-08-17 and made the phase-3 arms differ by more than the thing under test. The regret figures
# are already printed by generation itself.
say "PHASE 2 regret read-out (parsed from gen.log)"
grep -E "label noise|stderr|regret|EST\. win-turn" "$STAGE/gen.log" | tail -8 >> "$STATUS"

# --- phase 3: in-play A/B, staged profile vs what the deck ships today ---------------------------
# ONE pooled batch, both arms interleaved, chunked to 25 games so no single slow game strands a job.
# Core-seconds are only comparable because of that interleaving.
say "PHASE 3 in-play A/B (staged keep model vs shipped defaults), one pooled batch"
python3 - "$SD" "$SRC" "$STAGE" <<'PY'
import json, sys
sd, src, stage = sys.argv[1], sys.argv[2], sys.argv[3]
DECK = "Mirrorwing Dragon"
jobs = []
for arm, d in (("new", sd), ("old", src)):
    for b in (820000, 821000, 822000, 823000):
        for off in range(0, 500, 25):
            jobs.append({"name": "%s_s%d_off%d" % (arm, b, off),
                         "deck": "%s/%s.cod" % (d, DECK),
                         "profile": "%s/%s.profile.json" % (d, DECK),
                         "games": 25, "seed": b + off, "game_index": off,
                         "depth": 5, "budget_ms": 20, "abandon_units": 40000000})
json.dump({"jobs": jobs}, open(stage + "/ab.manifest.json", "w"))
print(len(jobs))
PY
./build/Release/mtg --batch "$STAGE/ab.manifest.json" > "$STAGE/ab.log" 2>&1
say "PHASE 3 exit=$?"
grep -E "^(new|old)_s" "$STAGE/ab.log" > "$STAGE/ab.rows.txt"
python3 scripts/ab2_report.py "$STAGE/ab.rows.txt" old > "$STAGE/ab.report.txt" 2>&1
cat "$STAGE/ab.report.txt" >> "$STATUS"

say "DONE. Nothing adopted. To adopt: copy $SD/$DECK.keepmodel.exhaustive.{profile,raw}.json* into $SRC/"

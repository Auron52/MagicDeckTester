#!/usr/bin/env bash
# RECIPE CAMPAIGN: characterize mulligan-gen RECIPES by (in-game QUALITY, gen SPEED) from ONE full-bottom
# raw per deck -- NO real adaptive_bottom gen. Adaptive bottoming is SIMULATED into a PLAYABLE profile
# (MTG_KEEP_SYNTH_ADAPTIVE_BOTTOM, validated to play like a real gen within realization noise); lower-R
# full bottoming is SYNTH_R. Every recipe is then tested with REAL games (batched, blind bottoming, the
# calib play setting: default depth + budget_ms=20 -- the footing the slivers +0.0057 was validated on).
#
# Recipes measured (delta vs full@R40 truth = the adopted-style baseline):
#   full@R40 (baseline)  full@R30  full@R20      -- lower-R full bottoming (SYNTH_R), S realizations
#   adaptive@R40  adaptive@R30  adaptive@R20     -- adaptive bottoming, A realizations (averaged)
# A single adaptive reconstruction is one "gen outcome" (noisy); we average A distinct ABOT_SEED
# realizations x seeds for a stable per-recipe number (the ~+0.002t reconstruction pessimism is consistent
# across recipes -> cancels in the ranking).
#
#   DECKS="slivers_vial burn Knights" bash scripts/recipe_campaign.sh
set -uo pipefail
cd "$(dirname "$0")/.."
BINA=build/RelWithDebInfo/mtg-analyze     # has the reconstruction (merge path); optimized
BIN=build/Release/mtg                     # goldfish batch (play binary, unchanged)
CARDS=src/cards/data/cards.json

declare -A DF=( [slivers_vial]=decks/slivers_vial/slivers_vial.txt [burn]=decks/burn/burn.txt \
  [Knights]=decks/Knights/Knights.cod [treasure_hunt]=decks/treasure_hunt/treasure_hunt.txt \
  [Anti-Lifegain]=decks/Anti-Lifegain/Anti-Lifegain.cod [Auras]=decks/Auras/Auras.cod \
  [Dragonstorm]=decks/Dragonstorm/Dragonstorm.cod )

DECKS=${DECKS:-"slivers_vial burn Knights"}
A_REALIZ=${A_REALIZ:-4}                    # adaptive reconstruction realizations (distinct ABOT_SEED)
S_REALIZ=${S_REALIZ:-2}                    # full-synth (SYNTH_R) realizations (distinct SYNTH_SEED)
GAMES=${GAMES:-300}
SEEDS=${SEEDS:-"4004 5005 6006 7007 8008 9009 10010 11011"}
BUDGET=${BUDGET:-20}
CAPS=${CAPS:-"30 20"}                      # lower-R points to test (R40 = the raw's native cap = baseline)

OUT=${OUT:-logs/recipe_campaign}; mkdir -p "$OUT"
stamp(){ date -u +%H:%M:%SZ; }
MASTER="$OUT/SUMMARY.txt"; : > "$MASTER"
log(){ echo "$*" | tee -a "$MASTER"; }
log "=== RECIPE CAMPAIGN ($(stamp))  A_realiz=$A_REALIZ S_realiz=$S_REALIZ games=$GAMES seeds=$(echo $SEEDS|wc -w) budget=$BUDGET ==="

# one pooled batch over SEEDS for a given exhaustive profile -> mean loss-penalized avg turn
play(){ local prof="$1" mf="$2" outlog="$3"
  MTG_EXHAUSTIVE_PROFILE="$prof" MTG_EXHAUSTIVE_BOTTOM=1 "$BIN" --batch "$mf" --threads 0 \
    > "$outlog" 2>/dev/null; }
mean_of(){ python3 - "$1" "$SEEDS" <<'PY'
import re,sys
fn=sys.argv[1]; seeds=set(sys.argv[2].split()); d={}
for ln in open(fn):
    m=re.match(r"s(\d+): played=\d+ avg=([\d.]+)",ln)
    if m and m.group(1) in seeds: d[m.group(1)]=float(m.group(2))
print(f"{sum(d.values())/len(d):.5f}" if d else "nan")
PY
}

for deck in $DECKS; do
  DECKFILE=${DF[$deck]:-}
  RAWGZ=$(ls decks/$deck/*.keepmodel.exhaustive.raw.json.gz 2>/dev/null | head -1)
  [ -n "$DECKFILE" ] && [ -e "$RAWGZ" ] || { log "SKIP $deck (no deck/raw)"; continue; }
  D="$OUT/$deck"; mkdir -p "$D"
  RAW="$D/raw.json"; gunzip -c "$RAWGZ" > "$RAW"
  BASEPROF="decks/$deck/$deck.profile.json"
  log ""; log "########## $deck ($(stamp)) ##########"

  # manifest (seeds pooled, no depth -> calib play setting; base profile carries card_scores/mulligan)
  MF="$D/mf.json"
  { echo '{ "jobs": ['; first=1
    for s in $SEEDS; do [ $first -eq 1 ] && first=0 || printf ',\n'
      printf '  { "name": "s%s", "deck": "%s", "profile": "%s", "games": %s, "seed": %s, "budget_ms": %s }' \
        "$s" "$DECKFILE" "$BASEPROF" "$GAMES" "$s" "$BUDGET"
    done; printf '\n] }\n'; } > "$MF"

  # --- baseline: full@R40 (plain merge, truth) ---
  play_prof(){ MTG_KEEP_MERGE=1 MTG_MERGE_INPUTS="$RAW" "$@" MTG_MERGE_OUT_PROFILE="$1x" MTG_MERGE_OUT_RAW=/tmp/ig.raw.json \
    "$BINA" "$DECKFILE" --cards-json "$CARDS" >/dev/null 2>&1; }
  MTG_KEEP_MERGE=1 MTG_MERGE_INPUTS="$RAW" MTG_MERGE_OUT_PROFILE="$D/full40.profile.json" MTG_MERGE_OUT_RAW=/tmp/ig.raw.json \
    "$BINA" "$DECKFILE" --cards-json "$CARDS" >/dev/null 2>&1
  play "$D/full40.profile.json" "$MF" "$D/play_full40.log"
  BASE=$(mean_of "$D/play_full40.log")
  log "  full@R40 (baseline)                    avg=$BASE"

  # helper: reconstruct + play a set of realizations, print averaged delta vs BASE
  run_recipe(){ local label="$1" tag="$2" nreal="$3"; shift 3   # rest = extra env for the merge
    local sumv=0 n=0 vals=""
    for r in $(seq 1 "$nreal"); do
      local pf="$D/${tag}_r$r.profile.json" lg="$D/play_${tag}_r$r.log"
      env "$@" MTG_KEEP_SYNTH_SEED=$((1000+r)) MTG_KEEP_SYNTH_ABOT_SEED=$((7000+r)) \
        MTG_KEEP_MERGE=1 MTG_MERGE_INPUTS="$RAW" MTG_MERGE_OUT_PROFILE="$pf" MTG_MERGE_OUT_RAW=/tmp/ig.raw.json \
        "$BINA" "$DECKFILE" --cards-json "$CARDS" >/dev/null 2>&1
      play "$pf" "$MF" "$lg"
      local v; v=$(mean_of "$lg"); vals="$vals $v"
    done
    python3 - "$label" "$BASE" "$vals" <<'PY' | tee -a "$MASTER"
import sys
label,base=sys.argv[1],float(sys.argv[2]); vals=[float(x) for x in sys.argv[3].split()]
m=sum(vals)/len(vals); import statistics
sd=statistics.pstdev(vals) if len(vals)>1 else 0.0
print(f"  {label:38} avg={m:.5f}  delta={m-base:+.4f}t  (n={len(vals)} sd={sd:.4f} vals={' '.join(f'{v-base:+.4f}' for v in vals)})")
PY
  }

  run_recipe "adaptive@R40" "ab40" "$A_REALIZ" MTG_KEEP_SYNTH_ADAPTIVE_BOTTOM=1
  for c in $CAPS; do
    run_recipe "full@R$c" "full$c" "$S_REALIZ" MTG_KEEP_SYNTH_R=$c
    run_recipe "adaptive@R$c" "ab$c" "$A_REALIZ" MTG_KEEP_SYNTH_R=$c MTG_KEEP_SYNTH_ADAPTIVE_BOTTOM=1
  done
  rm -f "$RAW"
done
log ""; log "=== CAMPAIGN DONE ($(stamp)) -- delta = recipe - full@R40; +ve = slower/worse. Speed: see the"
log "    regret sim's PERF line per (deck,cap). Fast recipe = cheapest recipe within your quality budget. ==="

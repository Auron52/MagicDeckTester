#!/usr/bin/env bash
# RECIPE CAMPAIGN: characterize mulligan-gen RECIPES by (in-game QUALITY, gen SPEED) from ONE full-bottom
# raw per deck -- NO real adaptive_bottom gen. Adaptive bottoming is SIMULATED into a PLAYABLE profile
# (MTG_KEEP_SYNTH_ADAPTIVE_BOTTOM, validated to play like a real gen within realization noise); lower keep-R
# full bottoming is SYNTH_R+KEEP_ONLY (sub-tables held at truth -> no uniform-downsample argmin artifact).
# Every recipe is tested with REAL games. Delta is vs full@native (the deck's complete gen = baseline).
#
# EFFICIENCY (see CLAUDE.md "Long runs MUST batch"): per deck we (1) reconstruct EVERY recipe/realization
# profile FIRST, in PARALLEL, then (2) run ONE pooled `mtg --batch` over all recipe x realization x seed
# jobs -- one load-imbalance tail per deck instead of one per (recipe,realization). The reconstructed
# profiles carry their exhaustive_keep, so each is named directly in the manifest (verified: identical play
# to env-layering); MTG_EXHAUSTIVE_BOTTOM=1 forces blind bottoming for all jobs.
#
#   DECKS="slivers_vial burn Knights" bash scripts/recipe_campaign.sh
set -uo pipefail
cd "$(dirname "$0")/.."
BINA=build/RelWithDebInfo/mtg-analyze      # reconstruction (merge path)
BIN=build/Release/mtg                      # goldfish batch
CARDS=src/cards/data/cards.json

declare -A DF=( [slivers_vial]=decks/slivers_vial/slivers_vial.txt [burn]=decks/burn/burn.txt \
  [Knights]=decks/Knights/Knights.cod [treasure_hunt]=decks/treasure_hunt/treasure_hunt.txt \
  [Anti-Lifegain]=decks/Anti-Lifegain/Anti-Lifegain.cod [Auras]=decks/Auras/Auras.cod \
  [Dragonstorm]=decks/Dragonstorm/Dragonstorm.cod )

DECKS=${DECKS:-"slivers_vial burn Knights"}
A_REALIZ=${A_REALIZ:-6}                     # adaptive reconstruction realizations (distinct ABOT_SEED)
S_REALIZ=${S_REALIZ:-3}                     # full-synth (SYNTH_R keep-only) realizations
GAMES=${GAMES:-400}
SEEDS=${SEEDS:-"4004 5005 6006 7007 8008 9009 10010 11011"}
BUDGET=${BUDGET:-20}
CAPS=${CAPS:-"30 20"}                       # lower keep-R points (native cap = full@native = baseline)
RECON_JOBS=${RECON_JOBS:-$(nproc)}          # parallel reconstruction fan-out

OUT=${OUT:-logs/recipe_campaign}; mkdir -p "$OUT"
stamp(){ date -u +%H:%M:%SZ; }
MASTER="$OUT/SUMMARY.txt"; : > "$MASTER"
log(){ echo "$*" | tee -a "$MASTER"; }
log "=== RECIPE CAMPAIGN ($(stamp)) A_realiz=$A_REALIZ S_realiz=$S_REALIZ games=$GAMES seeds=$(echo $SEEDS|wc -w) budget=$BUDGET recon_jobs=$RECON_JOBS ==="

# recipes as "label|tag|nreal|env..."; realization r appends distinct SYNTH/ABOT seeds.
build_recipes(){ RECIPES=(); RECIPES+=("full@native|full_nat|1|"); RECIPES+=("adaptive@native|ab_nat|$A_REALIZ|MTG_KEEP_SYNTH_ADAPTIVE_BOTTOM=1")
  for c in $CAPS; do
    RECIPES+=("full@R$c|full_$c|$S_REALIZ|MTG_KEEP_SYNTH_R=$c MTG_KEEP_SYNTH_KEEP_ONLY=1")
    RECIPES+=("adaptive@R$c|ab_$c|$A_REALIZ|MTG_KEEP_SYNTH_R=$c MTG_KEEP_SYNTH_KEEP_ONLY=1 MTG_KEEP_SYNTH_ADAPTIVE_BOTTOM=1")
  done; }

for deck in $DECKS; do
  DECKFILE=${DF[$deck]:-}
  RAWGZ=$(ls decks/$deck/*.keepmodel.exhaustive.raw.json.gz 2>/dev/null | head -1)
  [ -n "$DECKFILE" ] && [ -e "$RAWGZ" ] || { log "SKIP $deck (no deck/raw)"; continue; }
  D="$OUT/$deck"; mkdir -p "$D"; RAW="$D/raw.json"; gunzip -c "$RAWGZ" > "$RAW"
  BASEPROF="decks/$deck/$deck.profile.json"
  log ""; log "########## $deck ($(stamp)) ##########"
  build_recipes

  # --- Phase 1: reconstruct EVERY (recipe,realization) profile, in parallel (bounded fan-out) ---
  echo "  reconstructing profiles ($(stamp)) ..."
  running=0
  for spec in "${RECIPES[@]}"; do IFS='|' read -r label tag nreal env <<<"$spec"
    for r in $(seq 1 "$nreal"); do
      pf="$D/${tag}_r$r.profile.json"
      ( env $env MTG_KEEP_SYNTH_SEED=$((1000+r)) MTG_KEEP_SYNTH_ABOT_SEED=$((7000+r)) \
          MTG_KEEP_MERGE=1 MTG_MERGE_INPUTS="$RAW" MTG_MERGE_OUT_PROFILE="$pf" MTG_MERGE_OUT_RAW=/tmp/ig_${deck}_${tag}_$r.raw.json \
          "$BINA" "$DECKFILE" --cards-json "$CARDS" >/dev/null 2>&1 ) &
      running=$((running+1)); [ $((running % RECON_JOBS)) -eq 0 ] && wait
    done
  done
  wait

  # --- Phase 2: ONE pooled manifest over all (recipe,realization,seed); ONE batch -> one tail ---
  MF="$D/mf.json"
  { echo '{ "jobs": ['; first=1
    for spec in "${RECIPES[@]}"; do IFS='|' read -r label tag nreal env <<<"$spec"
      for r in $(seq 1 "$nreal"); do for s in $SEEDS; do
        [ $first -eq 1 ] && first=0 || printf ',\n'
        printf '  { "name": "%s|r%s|s%s", "deck": "%s", "profile": "%s", "games": %s, "seed": %s, "budget_ms": %s }' \
          "$tag" "$r" "$s" "$DECKFILE" "$D/${tag}_r$r.profile.json" "$GAMES" "$s" "$BUDGET"
      done; done
    done; printf '\n] }\n'; } > "$MF"
  echo "  playing one pooled batch ($(stamp)) ..."
  MTG_EXHAUSTIVE_BOTTOM=1 "$BIN" --batch "$MF" --threads 0 > "$D/batch.log" 2>"$D/batch.err"

  # --- Phase 3: aggregate per recipe (avg over realizations x seeds), delta vs full@native ---
  python3 - "$D/batch.log" "$MASTER" <<PY | tee -a "$MASTER"
import re,sys,statistics
log=open(sys.argv[1]); rows={}
for ln in log:
    m=re.match(r"(\S+?)\|r(\d+)\|s(\d+): played=\d+ avg=([\d.]+)",ln)
    if m: rows.setdefault(m.group(1),{}).setdefault(m.group(2),{})[m.group(3)]=float(m.group(4))
# per (tag, realization) = mean over seeds; recipe value = mean over realizations
recipes=[("full_nat","full@native (baseline)"),("ab_nat","adaptive@native (full keep)")]
for c in "$CAPS".split(): recipes+=[("full_%s"%c,"full@R%s"%c),("ab_%s"%c,"adaptive@R%s"%c)]
def recipe_mean(tag):
    if tag not in rows: return None,[]
    per=[sum(sv.values())/len(sv) for sv in rows[tag].values()]  # per-realization seed-mean
    return sum(per)/len(per), per
base,_=recipe_mean("full_nat")
for tag,label in recipes:
    m,per=recipe_mean(tag)
    if m is None: continue
    if tag=="full_nat": print(f"  {label:34} avg={m:.5f}"); continue
    sd=statistics.pstdev(per) if len(per)>1 else 0.0
    print(f"  {label:34} avg={m:.5f}  delta={m-base:+.4f}t  (n={len(per)} sd={sd:.4f})")
PY
  rm -f "$RAW"
done
log ""; log "=== CAMPAIGN DONE ($(stamp)) -- delta = recipe - full@native; +ve = slower/worse. ==="

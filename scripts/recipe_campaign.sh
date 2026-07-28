#!/usr/bin/env bash
# RECIPE CAMPAIGN: characterize mulligan-gen RECIPES by (in-game QUALITY, gen SPEED) from ONE full-bottom
# raw per deck -- NO real adaptive_bottom gen. Adaptive bottoming is SIMULATED into a PLAYABLE profile
# (MTG_KEEP_SYNTH_ADAPTIVE_BOTTOM, validated to play like a real gen); lower keep-R full bottoming is
# SYNTH_R + KEEP_ONLY (sub-tables held at truth). Each recipe is tested with REAL games. Delta vs full@native.
#
# MEMORY (learned the hard way): a big deck's exhaustive_keep profile is ~167 MB. Play used to hold every
# job's profile at once, so pooling all ~25 recipe profiles into one manifest OOMed. The batch runner now
# has an LRU profile cache (MTG_BATCH_PROFILE_CACHE, default 3) and sorts a profile's games contiguous, so
# ONE pooled play batch over all recipes stays memory-bounded (~3 resident profiles) AND keeps cores
# saturated to a single load-imbalance tail (CLAUDE.md "Long runs MUST batch" -- no more per-chunk tails).
# The only remaining guard is reconstruction: each merge parses the big raw, so it fans out RECON_JOBS-wide
# (24-way OOMs). Recon and play are sequential phases, so they never contend for memory.
#
#   DECKS="slivers_vial burn Knights" bash scripts/recipe_campaign.sh
set -uo pipefail
cd "$(dirname "$0")/.."
BINA=build/RelWithDebInfo/mtg-analyze
BIN=build/Release/mtg
CARDS=src/cards/data/cards.json

declare -A DF=( [slivers_vial]=decks/slivers_vial/slivers_vial.txt [burn]=decks/burn/burn.txt \
  [Knights]=decks/Knights/Knights.cod [treasure_hunt]=decks/treasure_hunt/treasure_hunt.txt \
  [Anti-Lifegain]=decks/Anti-Lifegain/Anti-Lifegain.cod [Auras]=decks/Auras/Auras.cod \
  [Dragonstorm]=decks/Dragonstorm/Dragonstorm.cod )

DECKS=${DECKS:-"slivers_vial burn Knights"}
A_REALIZ=${A_REALIZ:-6}
S_REALIZ=${S_REALIZ:-3}
GAMES=${GAMES:-400}
SEEDS=${SEEDS:-"4004 5005 6006 7007 8008 9009 10010 11011"}
BUDGET=${BUDGET:-20}
CAPS=${CAPS:-"30 20"}
RECON_JOBS=${RECON_JOBS:-4}          # parallel reconstructions (each parses the big raw -> memory-bound; 24 OOMs)
PCACHE=${PCACHE:-3}                  # play-batch LRU profile-cache cap (MTG_BATCH_PROFILE_CACHE)

OUT=${OUT:-logs/recipe_campaign}; mkdir -p "$OUT"
stamp(){ date -u +%H:%M:%SZ; }
MASTER="$OUT/SUMMARY.txt"; : > "$MASTER"
log(){ echo "$*" | tee -a "$MASTER"; }
log "=== RECIPE CAMPAIGN ($(stamp)) A_realiz=$A_REALIZ S_realiz=$S_REALIZ games=$GAMES seeds=$(echo $SEEDS|wc -w) budget=$BUDGET recon_jobs=$RECON_JOBS pcache=$PCACHE ==="

build_recipes(){ RECIPES=(); RECIPES+=("full@native|full_nat|1|"); RECIPES+=("adaptive@native (full keep)|ab_nat|$A_REALIZ|MTG_KEEP_SYNTH_ADAPTIVE_BOTTOM=1")
  for c in $CAPS; do
    RECIPES+=("full@R$c|full_$c|$S_REALIZ|MTG_KEEP_SYNTH_R=$c MTG_KEEP_SYNTH_KEEP_ONLY=1")
    RECIPES+=("adaptive@R$c|ab_$c|$A_REALIZ|MTG_KEEP_SYNTH_R=$c MTG_KEEP_SYNTH_KEEP_ONLY=1 MTG_KEEP_SYNTH_ADAPTIVE_BOTTOM=1")
  done; }

for deck in $DECKS; do
  DECKFILE=${DF[$deck]:-}
  RAWGZ=$(ls decks/$deck/*.keepmodel.exhaustive.raw.json.gz 2>/dev/null | head -1)
  [ -n "$DECKFILE" ] && [ -e "$RAWGZ" ] || { log "SKIP $deck (no deck/raw)"; continue; }
  D="$OUT/$deck"; mkdir -p "$D"; RAW="$D/raw.json"; gunzip -c "$RAWGZ" > "$RAW"
  DECKPROF="decks/$deck/$deck.profile.json"
  log ""; log "########## $deck ($(stamp)) ##########"
  build_recipes

  # --- Phase 1: reconstruct every (recipe,realization) profile, RECON_JOBS-wide (raw write skipped) ---
  echo "  reconstructing profiles (RECON_JOBS=$RECON_JOBS) ($(stamp)) ..."
  PROFLIST=()   # "tag_r<r>:<path>"
  running=0
  for spec in "${RECIPES[@]}"; do IFS='|' read -r label tag nreal env <<<"$spec"
    for r in $(seq 1 "$nreal"); do
      pf="$D/${tag}_r$r.profile.json"; PROFLIST+=("$tag|$pf")
      ( env $env MTG_KEEP_SYNTH_SEED=$((1000+r)) MTG_KEEP_SYNTH_ABOT_SEED=$((7000+r)) \
          MTG_KEEP_MERGE=1 MTG_MERGE_INPUTS="$RAW" MTG_MERGE_OUT_PROFILE="$pf" MTG_MERGE_OUT_RAW=/dev/null \
          "$BINA" "$DECKFILE" --cards-json "$CARDS" >/dev/null 2>&1 ) &
      running=$((running+1)); [ $((running % RECON_JOBS)) -eq 0 ] && wait
    done
  done
  wait
  # drop any zero-byte (should not happen now) so a corrupt profile never enters a manifest
  GOOD=(); for e in "${PROFLIST[@]}"; do p="${e#*|}"; [ -s "$p" ] && GOOD+=("$e") || echo "  WARN dropped empty $p" | tee -a "$MASTER"; done
  PROFLIST=("${GOOD[@]}")

  # --- Phase 2: ONE pooled batch over ALL recipe profiles (LRU cache caps memory; sort groups by profile) ---
  echo "  playing ${#PROFLIST[@]} profiles in ONE pooled batch (pcache=$PCACHE) ($(stamp)) ..."
  MF="$D/mf.json"
  { echo '{ "jobs": ['; first=1
    for e in "${PROFLIST[@]}"; do tag="${e%%|*}"; pf="${e#*|}"; r=$(basename "$pf" .profile.json); r=${r##*_}
      for s in $SEEDS; do [ $first -eq 1 ] && first=0 || printf ',\n'
        printf '  { "name": "%s|%s|s%s", "deck": "%s", "profile": "%s", "games": %s, "seed": %s, "budget_ms": %s }' \
          "$tag" "$r" "$s" "$DECKFILE" "$pf" "$GAMES" "$s" "$BUDGET"
      done
    done; printf '\n] }\n'; } > "$MF"
  MTG_BATCH_PROFILE_CACHE=$PCACHE MTG_EXHAUSTIVE_BOTTOM=1 "$BIN" --batch "$MF" --threads 0 \
    > "$D/batch.log" 2>"$D/batch.err"

  # --- Phase 3: aggregate per recipe (avg over realizations x seeds), delta vs full@native ---
  python3 - "$D/batch.log" "$CAPS" <<'PY' | tee -a "$MASTER"
import re,sys,statistics
rows={}
for ln in open(sys.argv[1]):
    m=re.match(r"(\S+?)\|(\w+)\|s(\d+): played=\d+ avg=([\d.]+)",ln)
    if m: rows.setdefault(m.group(1),{}).setdefault(m.group(2),{})[m.group(3)]=float(m.group(4))
recipes=[("full_nat","full@native (baseline)"),("ab_nat","adaptive@native (full keep)")]
for c in sys.argv[2].split(): recipes+=[("full_%s"%c,"full@R%s"%c),("ab_%s"%c,"adaptive@R%s"%c)]
def recipe_mean(tag):
    if tag not in rows: return None,[]
    per=[sum(sv.values())/len(sv) for sv in rows[tag].values()]
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

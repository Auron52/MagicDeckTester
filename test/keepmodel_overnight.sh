#!/bin/bash
# ============================================================================================
# OVERNIGHT keep-model SCALE test for Slivers  --  SET UP NOW, LAUNCH MANUALLY TONIGHT:
#     bash test/keepmodel_overnight.sh
#
# WHY: the analyzer's mulligan KEEP model currently trains on only ~1000 distinct opening hands
# (max(200, 2000/scale)), each a SINGLE clairvoyant library realisation -- far fewer samples than
# the land grid's ~576k games, so the policy (and the d5 A/B turn-deltas) are noisy/undertrained.
# This run re-fits the keep model at GRID-COMPARABLE scale (MTG_KEEP_GAMES) to de-noise it, and
# A/Bs it against the committed profile so we can see the REAL effect.
#
# MODE: keep-model-only (loads the COMMITTED profile, fits ONLY the keep tree onto it). NON-
# DESTRUCTIVE -- the committed decks/slivers_vial.profile.json is the read-only input; output goes
# to <deck>.keepmodel.profile.json which we stage under logs/. The A/B isolates exactly the keep
# model (committed land params on both arms). Nothing is committed or adopted.
#
# BUDGET MATH (measured): eps=0.02, depth=5  =>  ~1000 hands ~= 7.5 min, so ~12k hands ~= 1.5 h.
# Two eps points at 12k hands ~= 2.5-3 h of fitting + ~0.5-1 h of A/B  ~=  ~4 h total. Tune below.
#
# RUN A ~4h version, then (to check sample-size diminishing returns) a ~8h version -- separate dirs,
# so they can be compared directly:
#     bash test/keepmodel_overnight.sh                  # ~4h -> logs/keepmodel_overnight/slivers_vial/h12000_d5/
#     KM_HANDS=25000 bash test/keepmodel_overnight.sh   # ~8h -> logs/keepmodel_overnight/slivers_vial/h25000_d5/
#     KM_DECK=decks/test_deck.txt bash ...              # another deck (must have a committed profile)
#     KM_TAG=myrun   bash test/keepmodel_overnight.sh   # custom output subdir name
# Each run re-fits the keep model + re-A/Bs committed in its own dir; diff the two REPORT.txt to see
# whether 2x the hands meaningfully moves the policy/A/B (i.e. whether 4h was already enough).
# ============================================================================================
set -uo pipefail
cd "$(dirname "$0")/.."

# ---- TUNE THESE (env-overridable, e.g. KM_HANDS=300 KM_EPS="0.25" KM_AB_GAMES=50 for a smoke) --
HANDS=${KM_HANDS:-12000}                 # distinct opening hands per keep-model fit (sample size)
IFS=' ' read -r -a EPS_LIST <<< "${KM_EPS:-0.02 0.05}"   # reach-prob cutoffs to compare at scale:
                            #   0.02 -> decides down to hand size 4 (LEARNS the gi=35-class decision)
                            #   0.05 -> force-keep floor at size 4 (does NOT learn it; higher floor)
                            # Each eps is another full fit -- shrink HANDS to stay in budget.
DEPTH=${KM_DEPTH:-5}        # search depth the keep values roll out at (must match play depth)
AB_GAMES=${KM_AB_GAMES:-1000}            # games per A/B cell
FIT_TIMEOUT=${KM_FIT_TIMEOUT:-0}         # seconds; OPTIONAL wall-clock time-box per per-eps fit (0=off).
                            # NOT a runaway guard: the virtual (work-based) budget already makes each
                            # search a bounded, deterministic amount of work, so nothing runs away --
                            # a slow deck (e.g. Hinata) is just legitimately expensive, finite and
                            # predictable. Set this ONLY to deliberately skip a deck you KNOW is too
                            # slow for a fixed window (it discards that fit's compute -- there's no
                            # mid-fit checkpoint). For SLOW-BUT-VALID decks, leave it off; the real
                            # answer is per-deck time ESTIMATION (plan the window) + perf optimization.
# ---------------------------------------------------------------------------------------------

DECK=${KM_DECK:-decks/slivers_vial.txt}        # any already-analyzed deck (must have a committed profile)
STEM=$(basename "$DECK"); STEM=${STEM%.*}
COMMITTED=decks/$STEM.profile.json                                         # read-only input
BASELINE=logs/keepmodel_overnight/baselines/$STEM.committed-baseline.profile.json   # pristine A/B baseline
CARDS=src/cards/data/cards.json
OUT=logs/keepmodel_overnight/$STEM/${KM_TAG:-h${HANDS}_d${DEPTH}}   # per-deck, per-config dir (no clobber)
SEED=20260627               # fixed -> reproducible keep model
AB_SEEDS=(4004 5005 6006 7007)
AB_DEPTHS=(0 3 5)           # d0 shows the keep effect unmasked by search; d3/d5 are the real play depths

BIN=./build/Release/mtg; AN=./build/Release/mtg-analyze
mkdir -p "$OUT" "$(dirname "$BASELINE")"
REPORT="$OUT/REPORT.txt"; : > "$REPORT"
log(){ echo "$@" | tee -a "$REPORT"; }
stamp(){ date -u +%Y-%m-%dT%H:%M:%SZ; }
# RESUME support: each stage drops a done-marker on completion; a re-launch skips finished stages.
# (Granularity = committed-A/B and per-eps. A fit killed mid-way has no marker, so it re-runs that eps
# from scratch -- the keep-model fit itself is not yet checkpointable.) Set KM_FORCE=1 to redo all.
done_marker(){ [ -z "${KM_FORCE:-}" ] && [ -f "$OUT/.done_$1" ]; }
mark_done(){ touch "$OUT/.done_$1"; }

# Sanity / safety.
[ -x "$BIN" ] && [ -x "$AN" ] || { echo "build Release first: cmake --build build --config Release"; exit 1; }
[ -f "$BASELINE" ] || cp "$COMMITTED" "$BASELINE"   # snapshot committed as the immutable A/B baseline

# Run one A/B arm: dump per-game wins (for the audit) + aggregate won/avg, for each depth x seed.
ab(){ # $1=profile  $2=tag
  local prof=$1 tag=$2
  for d in "${AB_DEPTHS[@]}"; do
    for s in "${AB_SEEDS[@]}"; do
      MTG_DUMP_WINS=1 "$BIN" "$DECK" --profile "$prof" --seed "$s" --games "$AB_GAMES" \
        --depth "$d" --budget-ms 20 --max-turns 8 --lookahead-bottoming --threads 0 \
        > "$OUT/agg_${tag}_d${d}_s${s}.txt" 2> "$OUT/dump_${tag}_d${d}_s${s}.err"
      grep -oE 'gi=[0-9]+ wt=-?[0-9]+' "$OUT/dump_${tag}_d${d}_s${s}.err" \
        | sed -E 's/gi=([0-9]+) wt=(-?[0-9]+)/\1 \2/' | sort -n > "$OUT/wins_${tag}_d${d}_s${s}.wins"
      local won avg
      won=$(grep -oE 'Games won *: *[0-9]+' "$OUT/agg_${tag}_d${d}_s${s}.txt"|grep -oE '[0-9]+$')
      avg=$(grep -oE 'Avg win turn *: *[0-9.]+' "$OUT/agg_${tag}_d${d}_s${s}.txt"|grep -oE '[0-9.]+$')
      log "    d$d s$s: won=${won:-?} avg=${avg:-?}"
    done
  done
}

# Per-game flip classification of an arm vs committed (no re-run -- diffs the dumped .wins).
flips(){ # $1=tag
  local tag=$1
  for d in "${AB_DEPTHS[@]}"; do for s in "${AB_SEEDS[@]}"; do
    python3 - "$OUT/wins_committed_d${d}_s${s}.wins" "$OUT/wins_${tag}_d${d}_s${s}.wins" "$d" "$s" <<'PY' | tee -a "$REPORT"
import sys
gt={};nw={}
for fn,dd in ((sys.argv[1],gt),(sys.argv[2],nw)):
    try:
        for ln in open(fn):
            a=ln.split()
            if len(a)>=2: dd[int(a[0])]=int(a[1])
    except FileNotFoundError: pass
wl=lw=f=sl=0; wlg=[]
for gi in gt:
    g=gt[gi]; n=nw.get(gi,g)
    if g==n: continue
    if g>0 and n<0: wl+=1; wlg.append(gi)
    elif g<0 and n>0: lw+=1
    elif 0<n<g: f+=1
    elif 0<g<n: sl+=1
print(f"    d{sys.argv[3]} s{sys.argv[4]}: WIN->LOSS={wl}{(' '+str(wlg[:8])) if wlg else ''} LOSS->WIN={lw} faster={f} slower={sl}")
PY
  done; done
}

log "=== keep-model OVERNIGHT scale test ($(stamp)) ==="
log "HANDS=$HANDS  EPS_LIST=${EPS_LIST[*]}  DEPTH=$DEPTH  seed=$SEED"
log ""
if done_marker committed; then log "--- committed baseline A/B: SKIP (resumed) ---"; else
  log "--- committed baseline A/B ---"
  ab "$BASELINE" committed
  mark_done committed
fi

for eps in "${EPS_LIST[@]}"; do
  if done_marker "km$eps"; then log ""; log "================ eps=$eps: SKIP (resumed) ================"; continue; fi
  log ""; log "================ keep-model  eps=$eps  HANDS=$HANDS  ($(stamp)) ================"
  t0=$SECONDS
  TO=(); [ "$FIT_TIMEOUT" -gt 0 ] && TO=(timeout "$FIT_TIMEOUT")
  MTG_KEEP_MODEL_ONLY=1 MTG_KEEP_GAMES=$HANDS MTG_KEEP_REACH_EPS=$eps \
    MTG_ANALYZE_DEPTH=$DEPTH MTG_ANALYZE_SCALE=2 \
    "${TO[@]}" "$AN" "$DECK" --cards-json "$CARDS" --max-turns 8 --seed "$SEED" \
    > "$OUT/fit_eps$eps.log" 2>&1
  rc=$?; log "fit rc=$rc time=$((SECONDS-t0))s"
  [ $rc -eq 124 ] && { log "  FIT TIMED OUT (>${FIT_TIMEOUT}s, KM_FIT_TIMEOUT) -- skipped, no marker; re-run later"; continue; }
  grep -E "training a decision|reached p=|decisions at hand sizes|chose depth|accuracy bar|always-keep|land_count" \
    "$OUT/fit_eps$eps.log" | sed 's/^/    /' | tee -a "$REPORT"
  [ $rc -eq 0 ] || { log "  FIT FAILED -- see $OUT/fit_eps$eps.log"; continue; }
  prof="$OUT/km_eps$eps.profile.json"
  # The analyzer (MTG_KEEP_MODEL_ONLY) writes <deck-stem>.keepmodel.profile.json next to the deck;
  # derive it from $STEM so non-slivers decks don't silently A/B the slivers model.
  cp "decks/$STEM.keepmodel.profile.json" "$prof"
  log "  Rules:"; sed -n '/Rules:/,/deck_colors/p' "$OUT/fit_eps$eps.log" | sed 's/^/    /' | tee -a "$REPORT"
  log "--- A/B  (keep-model eps=$eps  vs committed land params) ---"
  ab "$prof" "km$eps"
  log "  per-game flips vs committed:"
  flips "km$eps"
  mark_done "km$eps"
done

log ""; log "=== DONE ($(stamp)) ==="
log "Staged keep-model profiles: $OUT/km_eps*.profile.json   (committed profile untouched)"
log "Review: aggregate above + any WIN->LOSS gi in the flip lines (reproduce single-game to root-cause)."

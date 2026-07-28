#!/usr/bin/env bash
# Reusable EMPIRICAL R-sweep for the exhaustive KEEP policy via chunked generation + cumulative merge.
# Generates N independent uniform-R rollout chunks (distinct seed_base), then for each target chunk-count
# k pools chunks 1..k into a policy at effective R = k*CHUNK_R and A/Bs the exhaustive KEEP against static
# (keep isolation: bottoming OFF both sides) so the win-turn delta is purely the keep decision's quality
# at that R. Chunks are cheap for small-K decks (e.g. test_deck/burn, K=9). Re-runnable: existing chunks
# are reused, so you can add chunks later (resume) and re-sweep. Nothing here touches committed profiles.
#
#   KM_DECK=decks/test_deck.txt KM_CHUNK_R=10 KM_NUM_CHUNKS=6 KM_TARGETS="3 4 5 6" bash test/keepmodel_r_sweep.sh
set -u
BIN=./build/Release/mtg
ANALYZE=./build/Release/mtg-analyze
DECK=${KM_DECK:?set KM_DECK=decks/<name>.txt}
STEM=$(basename "$DECK" | sed -E 's/\.(txt|cod)$//')
STATIC=decks/$STEM.profile.json
[ -f "$STATIC" ] || { echo "missing static profile: $STATIC"; exit 1; }

CHUNK_R=${KM_CHUNK_R:-10}                 # uniform rollouts per chunk (floor=0 => uniform = R)
NUM_CHUNKS=${KM_NUM_CHUNKS:-6}
SEED_BASE=${KM_SEED_BASE:-20260801}       # chunk i uses seed_base SEED_BASE+i (disjoint => poolable)
TARGETS=${KM_TARGETS:-"3 4 5 6"}          # chunk-counts to pool -> R = k*CHUNK_R
# Generation rollout depth/budget = MTG_EQUIV_DEPTH/BUDGET (unified: bucketing + rollouts + play_digest).
# d3/b10 is the combo-deck default (antilife: ~12x faster than d5 at GT-equal win-turn). MUST match across
# machines you intend to pool (part of the bucket_fp / play_digest identity).
GEN_DEPTH=${KM_GEN_DEPTH:-3}
GEN_BUDGET=${KM_GEN_BUDGET:-10}
# Chunk floor: == CHUNK_R => UNIFORM (every cell exactly CHUNK_R => exact R=k*CHUNK_R pooling; clean for an
# R-sweep). Set KM_CHUNK_FLOOR < CHUNK_R for ADAPTIVE chunks (cheaper, per-cell R varies => nominal pooled
# R; what an expensive real run like Hinata uses). NOTE: MTG_KEEP_R_FLOOR has env-min 1, so 0 != uniform.
CHUNK_FLOOR=${KM_CHUNK_FLOOR:-$CHUNK_R}
AB_SEEDS=${KM_AB_SEEDS:-"4004 5005 6006 7007 8008 9009 10010 11011 12012 13013 14014 15015 16016 17017 18018 19019"}
AB_DEPTH=${KM_AB_DEPTH:-3}
AB_GAMES=${KM_AB_GAMES:-1000}
AB_BUDGET=${KM_AB_BUDGET:-20}

CH=logs/${STEM}_rsweep/chunks;  mkdir -p "$CH"
OUT=logs/${STEM}_rsweep;        mkdir -p "$OUT"
RES=$OUT/RESULTS.txt; : > "$RES"
stamp(){ date -u +%H:%M:%SZ; }
log(){ echo "$*" | tee -a "$RES"; }
log "=== $STEM empirical R-sweep (chunk_R=$CHUNK_R, $NUM_CHUNKS chunks; gen d$GEN_DEPTH/b$GEN_BUDGET; A/B d$AB_DEPTH/${AB_GAMES}g/b$AB_BUDGET) $(stamp) ==="

# ---- Phase 1: generate uniform-R chunks (skip existing) --------------------------------------------
for i in $(seq 1 "$NUM_CHUNKS"); do
  craw=$CH/chunk_${i}.raw.json
  if [ -f "$craw" ]; then log "chunk $i: exists ($craw)"; continue; fi
  s=$((SEED_BASE + i))
  log "chunk $i: generating uniform R=$CHUNK_R seed_base=$s $(stamp)"
  # Direct-write the chunk raw; skip the (unneeded) per-chunk profile via empty MTG_KEEP_OUT_PROFILE.
  MTG_KEEP_EXHAUSTIVE=1 MTG_KEEP_ROLLOUTS=$CHUNK_R MTG_KEEP_R_FLOOR=$CHUNK_FLOOR \
    MTG_EQUIV_DEPTH=$GEN_DEPTH MTG_EQUIV_BUDGET=$GEN_BUDGET \
    MTG_KEEP_OUT_RAW="$craw" MTG_KEEP_OUT_PROFILE= \
    "$ANALYZE" "$DECK" --seed "$s" >"$OUT/gen_${i}.log" 2>&1
done

# ---- Phase 2: static baseline A/B (keep isolation), once ------------------------------------------
staticdir=$OUT/wins_static; mkdir -p "$staticdir"
if [ ! -f "$staticdir/.done" ]; then
  log "static baseline A/B $(stamp)"
  # MTG_EXHAUSTIVE_PROFILE=none forces a GENUINE static arm: if this deck has an adopted
  # `decks/<deck>.keepmodel.exhaustive.profile.json[.gz]`, presence-gated auto-attach would otherwise
  # layer it onto the baseline too (both arms identical => false 0.0 delta). Replaces the old
  # "mv the .gz out of decks/" workaround.
  for s in $AB_SEEDS; do
    MTG_EXHAUSTIVE_PROFILE=none MTG_DUMP_WINS=1 MTG_EXHAUSTIVE_BOTTOM=0 "$BIN" "$DECK" --profile "$STATIC" --seed "$s" \
      --games "$AB_GAMES" --depth "$AB_DEPTH" --budget-ms "$AB_BUDGET" --max-turns 8 \
      --lookahead-bottoming --threads 0 >/dev/null 2>"$staticdir/err_s${s}.txt"
  done
  touch "$staticdir/.done"
fi

# ---- Phase 3: cumulative merge + exhaustive-keep A/B per target -----------------------------------
log ""
log "R      exh_d${AB_DEPTH}   static    delta      (neg = exh faster)"
for k in $TARGETS; do
  inputs=""; for i in $(seq 1 "$k"); do inputs="$inputs${inputs:+,}$CH/chunk_${i}.raw.json"; done
  R=$((k * CHUNK_R))
  prof=/tmp/${STEM}_rsweep_R${R}.profile.json
  MTG_KEEP_MERGE=1 MTG_MERGE_INPUTS="$inputs" \
    MTG_MERGE_OUT_PROFILE="$prof" MTG_MERGE_OUT_RAW=/tmp/${STEM}_rsweep_R${R}.raw.json \
    "$ANALYZE" "$DECK" >"$OUT/merge_R${R}.log" 2>&1
  expdir=$OUT/wins_R${R}; mkdir -p "$expdir"
  for s in $AB_SEEDS; do
    MTG_DUMP_WINS=1 MTG_EXHAUSTIVE_BOTTOM=0 "$BIN" "$DECK" --profile "$prof" --seed "$s" \
      --games "$AB_GAMES" --depth "$AB_DEPTH" --budget-ms "$AB_BUDGET" --max-turns 8 \
      --lookahead-bottoming --threads 0 >/dev/null 2>"$expdir/err_s${s}.txt"
  done
  python3 - "$expdir" "$staticdir" "$R" "$AB_SEEDS" >>"$RES" <<'PY'
import sys,os
exp,stat,R,seedstr=sys.argv[1],sys.argv[2],sys.argv[3],sys.argv[4]
seeds=[int(x) for x in seedstr.split()]
def wins(fn):
    w={}
    if os.path.exists(fn):
        for ln in open(fn):
            if ln.startswith("[win]"):
                p=ln.split(); w[int(p[1].split('=')[1])]=int(p[2].split('=')[1])
    return w
def mpos(w):
    v=[t for t in w.values() if t>0]; return sum(v)/len(v) if v else 0.0
E=[mpos(wins(f"{exp}/err_s{s}.txt")) for s in seeds]
St=[mpos(wins(f"{stat}/err_s{s}.txt")) for s in seeds]
e=sum(E)/len(E); st=sum(St)/len(St)
print(f"{R:<6} {e:>7.4f}  {st:>7.4f}   {e-st:>+8.4f}")
PY
  rm -f "$prof" /tmp/${STEM}_rsweep_R${R}.raw.json
done
log ""
log "=== DONE $(stamp) ==="

#!/usr/bin/env bash
# In-game A/B of two EXHAUSTIVE keep+bottom profiles for the SAME deck, played identically.
#
# The intended use is the "reconstruct-and-compare" recipe (docs/design/mulligan-reconstruct-lower-r.md):
# generate ONE full-R exhaustive profile, then reconstruct cheaper variants from its raw sidecar with NO
# re-rollout (MTG_KEEP_SYNTH_R = lower R; MTG_KEEP_SYNTH_BOTTOM_R = adaptive bottoming) and A/B each
# reconstruction against the full profile here. The reconstruction's self-reported D_opt is winner's-curse
# optimistic and must NOT be used to judge quality -- THIS in-game delta is the ground truth.
#
# Mechanism: MTG_EXHAUSTIVE_PROFILE=<path> attaches THAT profile's exhaustive block onto the deck's base
# profile (no decks/ churn, no GT churn). Both arms force blind exhaustive bottoming on
# (MTG_EXHAUSTIVE_BOTTOM=1) so each profile's stored bottom_keep TARGETS are actually exercised.
#
#   negative overall delta  => B (candidate) wins EARLIER than A (baseline) -- cheaper AND no cost.
#   ~0 delta (within noise)  => the cheaper reconstruction is sufficient for this deck.
#
# Vars (all overridable):
#   KM_DECK   deck file                 (default Dragonstorm)
#   KM_BASE   deck base .profile.json   (default sibling of KM_DECK)
#   A_PROF/A_TAG  baseline profile+tag  (default the full-R profile)
#   B_PROF/B_TAG  candidate profile+tag (default a reconstruction)
#   KM_AB_SEEDS/KM_AB_DEPTHS/KM_AB_GAMES   grid (defaults: held-out seeds, d0/3/5, 1000 games)
# Seeds default to a HELD-OUT band (4004..19019) disjoint from the regression train/overnight seeds.
set -u
cd "$(dirname "$0")/.."

BIN=./build/Release/mtg
CARDS=src/cards/data/cards.json
KM_DECK=${KM_DECK:-decks/Dragonstorm/Dragonstorm.cod}
KM_BASE=${KM_BASE:-"${KM_DECK%.*}.profile.json"}
A_PROF=${A_PROF:?set A_PROF to the baseline (full-R) exhaustive profile}
B_PROF=${B_PROF:?set B_PROF to the candidate (reconstructed) exhaustive profile}
A_TAG=${A_TAG:-full}
B_TAG=${B_TAG:-candidate}

SEEDS=${KM_AB_SEEDS:-"4004 5005 6006 7007 8008 9009 10010 11011 12012 13013 14014 15015 16016 17017 18018 19019"}
DEPTHS=${KM_AB_DEPTHS:-"0 3 5"}
GAMES=${KM_AB_GAMES:-1000}
BUDGET_MS=${KM_AB_BUDGET_MS:-20}
MAXT=${KM_AB_MAXT:-8}

OUT=${KM_AB_OUT:-logs/keep_reconstruct_ab}; mkdir -p "$OUT"
REPORT="$OUT/REPORT.txt"
stamp(){ date -u +%Y-%m-%dT%H:%M:%SZ; }
log(){ echo "$*" | tee -a "$REPORT"; }
: > "$REPORT"
log "=== keep-reconstruct in-game A/B ($(stamp)) deck=$KM_DECK ==="
log "seeds[$SEEDS] depths[$DEPTHS] games=$GAMES budget-ms=$BUDGET_MS max-turns=$MAXT  (both arms MTG_EXHAUSTIVE_BOTTOM=1)"
log "A(baseline)=$A_TAG   $A_PROF"
log "B(candidate)=$B_TAG  $B_PROF"

# ab <exhaustive-profile> <tag> -- attach it via MTG_EXHAUSTIVE_PROFILE, blind exhaustive bottoming ON.
ab(){ local prof="$1" tag="$2" d s
  for d in $DEPTHS; do for s in $SEEDS; do
    MTG_EXHAUSTIVE_PROFILE="$prof" MTG_DUMP_WINS=1 MTG_EXHAUSTIVE_BOTTOM=1 \
      "$BIN" "$KM_DECK" --profile "$KM_BASE" --seed "$s" --games "$GAMES" --depth "$d" \
      --budget-ms "$BUDGET_MS" --max-turns "$MAXT" --lookahead-bottoming --threads 0 \
      > "$OUT/wins_${tag}_d${d}_s${s}.out" 2> "$OUT/err_${tag}_d${d}_s${s}.txt"
  done; done; }

log "--- arm=$A_TAG ($(stamp)) ---"; ab "$A_PROF" "$A_TAG"
log "--- arm=$B_TAG ($(stamp)) ---"; ab "$B_PROF" "$B_TAG"

python3 - "$OUT" "$A_TAG" "$B_TAG" "$DEPTHS" "$SEEDS" <<'PY' | tee -a "$REPORT"
import sys, os
OUT, base, cand = sys.argv[1], sys.argv[2], sys.argv[3]
depths = [int(x) for x in sys.argv[4].split()]
seeds  = [int(x) for x in sys.argv[5].split()]
def wins(tag, d, s):
    fn = f"{OUT}/err_{tag}_d{d}_s{s}.txt"; w = {}
    if not os.path.exists(fn): return w
    for ln in open(fn):
        if ln.startswith("[win]"):
            p = ln.split(); w[int(p[1].split('=')[1])] = int(p[2].split('=')[1])
    return w
def mean(xs): return sum(xs)/len(xs) if xs else 0.0
print(f"\n=== in-game A/B ({cand} vs {base}; negative delta = {cand} wins earlier) ===")
print(f"{'depth':<6}{base:>10}{cand:>10}{'deltaB-A':>10}{'seedsB<0':>10}")
grand = []
for d in depths:
    da, db, sdel = [], [], 0
    for s in seeds:
        wa, wb = wins(base, d, s), wins(cand, d, s)
        gi = set(wa) & set(wb)                      # games both arms won -> comparable win-turns
        if not gi: continue
        ma = mean([wa[g] for g in gi]); mb = mean([wb[g] for g in gi])
        da.append(ma); db.append(mb); sdel += (mb - ma < -1e-9)
    A, B = mean(da), mean(db)
    print(f"{d:<6}{A:>10.4f}{B:>10.4f}{B-A:>10.4f}{sdel:>10}")
    grand.append(B - A)
print(f"\noverall mean delta (B-A) = {mean(grand):+.4f} turns  "
      f"({'candidate cheaper-and->= as good' if mean(grand) <= 1e-3 else 'candidate costs win-turn'})")
PY
log ""
log "done $(stamp)"

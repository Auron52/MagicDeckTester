#!/usr/bin/env bash
# In-game A/B of two EXHAUSTIVE keep+bottom profiles for the SAME deck, played identically with the
# deck's REAL PLAY PROFILE (value_play) and the BATCH runner (one pooled work queue per arm).
#
# Intended for the reconstruct-and-compare recipe (docs/design/mulligan-reconstruct-lower-r.md):
# generate ONE full-R profile, reconstruct cheaper variants from its raw with NO re-rollout
# (MTG_KEEP_SYNTH_R = lower R; MTG_KEEP_SYNTH_BOTTOM_R = adaptive bottoming) and A/B each here.
# The reconstruction's self-reported D_opt is winner's-curse optimistic; THIS in-game delta is truth.
#
# Each profile is layered via MTG_EXHAUSTIVE_PROFILE onto the base profile's value_play (which owns the
# play depth -- the manifest omits "depth"). Bottoming follows each profile's own baked flag. Each arm
# runs as ONE `mtg --batch` over all seeds -> a single pooled queue (one tail per arm, not one per seed).
#
#   A_PROF=<full.gz> A_TAG=full  B_PROF=<synthR15.json> B_TAG=r15  KM_DECK=decks/x/x.cod \
#     bash test/keep_reconstruct_ab.sh
set -uo pipefail
cd "$(dirname "$0")/.."
BIN=./build/Release/mtg
KM_DECK=${KM_DECK:-decks/Dragonstorm/Dragonstorm.cod}
STEM=$(basename "$KM_DECK"); STEM=${STEM%.*}
KM_BASE=${KM_BASE:-"$(dirname "$KM_DECK")/$STEM.profile.json"}    # carries value_play (PLAY profile)
A_PROF=${A_PROF:?set A_PROF to the baseline (full-R) exhaustive profile}
B_PROF=${B_PROF:?set B_PROF to the candidate (reconstructed) exhaustive profile}
A_TAG=${A_TAG:-full}
B_TAG=${B_TAG:-candidate}
[ -e "$A_PROF" ] || { echo "missing A_PROF: $A_PROF"; exit 1; }
[ -e "$B_PROF" ] || { echo "missing B_PROF: $B_PROF"; exit 1; }
[ -e "$KM_BASE" ] || { echo "missing base profile (value_play): $KM_BASE"; exit 1; }

SEEDS=${KM_AB_SEEDS:-"4004 5005 6006 7007 8008 9009 10010 11011 12012 13013 14014 15015 16016 17017 18018 19019"}
GAMES=${KM_AB_GAMES:-1000}
BUDGET=${KM_AB_BUDGET:-20}

OUT=${KM_AB_OUT:-logs/keep_reconstruct_ab}; mkdir -p "$OUT"
REPORT="$OUT/REPORT.txt"
stamp(){ date -u +%Y-%m-%dT%H:%M:%SZ; }
log(){ echo "$*" | tee -a "$REPORT"; }
: > "$REPORT"
nseed=$(echo $SEEDS | wc -w)
log "=== keep-reconstruct A/B ($(stamp)) deck=$STEM  [batched, play-profile] ==="
log "seeds=$nseed games=$GAMES budget-ms=$BUDGET (value_play owns depth)"
log "A(baseline)=$A_TAG  $A_PROF"
log "B(candidate)=$B_TAG $B_PROF"

MF=$OUT/manifest.json
{ echo '{ "jobs": ['; first=1
  for s in $SEEDS; do [ $first -eq 1 ] && first=0 || printf ',\n'
    printf '  { "name": "s%s", "deck": "%s", "profile": "%s", "games": %s, "seed": %s, "budget_ms": %s }' \
      "$s" "$KM_DECK" "$KM_BASE" "$GAMES" "$s" "$BUDGET"
  done; printf '\n] }\n'; } > "$MF"

run_arm(){ local tag="$1" prof="$2"
  MTG_EXHAUSTIVE_PROFILE="$prof" "$BIN" --batch "$MF" --threads 0 --game-log-dir "$OUT/wins_$tag" \
    > "$OUT/batch_$tag.log" 2>"$OUT/batch_$tag.err"; }
log "--- arm A=$A_TAG ($(stamp)) ---"; run_arm "$A_TAG" "$A_PROF"
log "--- arm B=$B_TAG ($(stamp)) ---"; run_arm "$B_TAG" "$B_PROF"

python3 - "$OUT" "$A_TAG" "$B_TAG" "$SEEDS" <<'PY' | tee -a "$REPORT"
import sys, os, re
OUT, A, B, seeds = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4].split()
def avgs(tag):
    d = {}; fn = f"{OUT}/batch_{tag}.log"
    if os.path.exists(fn):
        for ln in open(fn):
            m = re.match(r"s(\d+): played=\d+ avg=([\d.]+)", ln)
            if m: d[m.group(1)] = float(m.group(2))
    return d
def mean(x): return sum(x)/len(x) if x else 0.0
a, b = avgs(A), avgs(B)
per = [(s, a[s], b[s]) for s in seeds if s in a and s in b]
mA, mB = mean([x[1] for x in per]), mean([x[2] for x in per])
nlt = sum(1 for _, x, y in per if y - x < -1e-9)
print(f"\n=== A/B ({B} vs {A}; negative delta = {B} wins earlier) — avg win turn (loss-penalized) ===")
print(f"{A:>14}{B:>14}{'delta B-A':>14}{'seeds B<A':>14}")
print(f"{mA:>14.4f}{mB:>14.4f}{mB-mA:>+14.4f}{str(nlt)+'/'+str(len(per)):>14}")
print(f"\noverall delta {mB-mA:+.4f}t  ({'candidate cheaper-and->= as good' if mB-mA <= 1e-3 else 'candidate costs win-turn'})")
PY
log "=== keep-reconstruct A/B DONE ($(stamp)) ==="

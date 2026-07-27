#!/usr/bin/env bash
# Reconstruct cheaper Dragonstorm profiles from the ONE full R=40 raw (no re-rollout) and A/B each
# against the full adopted profile IN GAME, using the deck's real PLAY PROFILE (value_play) and the
# BATCH runner (one pooled work queue per arm -> CPU-saturated, one load-imbalance tail per arm, not
# one per seed). Answers required-R knee (R=15..40 by 5) and adaptive-vs-full bottoming from one gen.
# See docs/design/mulligan-reconstruct-lower-r.md.
set -u
cd /workspaces/MagicDeckTester2

BINA=build/Release/mtg-analyze          # has the SYNTH reconstruction (merge path)
BIN=build/Release/mtg                   # goldfish (batch)
DECK=decks/Dragonstorm/Dragonstorm.cod
BASE=decks/Dragonstorm/Dragonstorm.profile.json     # carries value_play == the PLAY profile
CARDS=src/cards/data/cards.json
RAW=logs/Dragonstorm_gen/full_R40_e566eda.raw.json  # uncompressed full raw (merge input)
FULL=decks/Dragonstorm/Dragonstorm.keepmodel.exhaustive.profile.json.gz   # adopted full-R baseline
OUT=logs/keep_reconstruct; mkdir -p "$OUT"
IGN=/tmp/reconstruct_ignore.raw.json

SEEDS="4004 5005 6006 7007 8008 9009 10010 11011 12012 13013 14014 15015"   # 12 held-out
GAMES=${SWEEP_GAMES:-500}
BUDGET=${SWEEP_BUDGET:-20}                            # value_play owns depth; budget is the CLI knob
RLEVELS="15 20 25 30 35 40"

LOG=$OUT/sweep.log
exec > "$LOG" 2>&1
stamp(){ date -u +%H:%M:%SZ; }
nseed=$(echo $SEEDS | wc -w)
echo "=== reconstruct sweep (BATCHED, play-profile) $(stamp): R{$RLEVELS}+adaptbottom vs full; ${GAMES}g x${nseed} seeds ==="
[ -s "$RAW" ] || { echo "!! missing full raw $RAW"; exit 1; }

# --- 1. reconstruct variants from the full raw (no rollouts, deterministic) ---
build_variant(){ local tag="$1"; shift
  env "$@" MTG_KEEP_MERGE=1 MTG_MERGE_INPUTS="$RAW" \
    MTG_MERGE_OUT_PROFILE="$OUT/$tag.profile.json" MTG_MERGE_OUT_RAW="$IGN" \
    "$BINA" "$DECK" --cards-json "$CARDS" 2>&1 | grep -E "SYNTH-R|merged policy" | sed "s/^/  [$tag] /"; }
echo "--- reconstruct variants $(stamp) ---"
for k in $RLEVELS; do build_variant "R$k" MTG_KEEP_SYNTH_R=$k; done
build_variant "adaptbottom" MTG_KEEP_SYNTH_BOTTOM_R=1     # keep full, bottom sub-tables at floor R=1

# --- 2. ONE manifest, seeds pooled, PLAY-PROFILE driven (no "depth" key -> value_play owns depth) ---
MF=$OUT/manifest.json
{ echo '{ "jobs": ['; first=1
  for s in $SEEDS; do [ $first -eq 1 ] && first=0 || printf ',\n'
    printf '  { "name": "s%s", "deck": "%s", "profile": "%s", "games": %s, "seed": %s, "budget_ms": %s }' \
      "$s" "$DECK" "$BASE" "$GAMES" "$s" "$BUDGET"
  done; printf '\n] }\n'; } > "$MF"

# --- 3. each arm = ONE pooled batch; exhaustive keep/bottom profile via env, bottoming follows its flag ---
run_arm(){ local tag="$1" prof="$2"
  MTG_EXHAUSTIVE_PROFILE="$prof" "$BIN" --batch "$MF" --threads 0 --game-log-dir "$OUT/wins_$tag" \
    > "$OUT/batch_$tag.log" 2>"$OUT/batch_$tag.err"; }
echo "--- play full baseline $(stamp) ---"; run_arm full "$FULL"
for k in $RLEVELS; do echo "--- play R$k $(stamp) ---"; run_arm "R$k" "$OUT/R$k.profile.json"; done
echo "--- play adaptbottom $(stamp) ---"; run_arm adaptbottom "$OUT/adaptbottom.profile.json"

# --- 4. summary: per-seed loss-penalized avg (from batch stdout) -> delta vs full ---
python3 - "$OUT" "$SEEDS" "$RLEVELS" <<'PY'
import sys, os, re
OUT, seeds = sys.argv[1], sys.argv[2].split()
variants = [f"R{k}" for k in sys.argv[3].split()] + ["adaptbottom"]
def avgs(tag):
    d = {}; fn = f"{OUT}/batch_{tag}.log"
    if os.path.exists(fn):
        for ln in open(fn):
            m = re.match(r"s(\d+): played=\d+ avg=([\d.]+)", ln)
            if m: d[m.group(1)] = float(m.group(2))
    return d
def mean(x): return sum(x)/len(x) if x else 0.0
full = avgs("full")
print(f"\nfull baseline avg-win-turn (play-profile value_play d5, {len(seeds)} seeds): "
      f"{mean([full[s] for s in seeds if s in full]):.4f}")
print(f"\n{'variant':<12}{'avg':>10}{'delta vs full':>16}")
for v in variants:
    a = avgs(v); deltas = [a[s]-full[s] for s in seeds if s in a and s in full]
    print(f"{v:<12}{mean([a[s] for s in seeds if s in a]):>10.4f}{mean(deltas):>+16.4f}")
print("\ndelta = variant - full(R=40); +ve = variant SLOWER/worse. ~0 within noise => that R (or adaptive"
      " bottoming) is sufficient. Small deltas (<~0.02t) are at/below this grid's noise floor.")
PY
echo "=== sweep DONE $(stamp) ==="

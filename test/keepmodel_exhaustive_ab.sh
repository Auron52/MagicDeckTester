#!/usr/bin/env bash
# In-game A/B for the EXHAUSTIVE bucketed keep/bottom policy (see docs/design/exhaustive-keep-policy.md).
# Two disjoint isolations, selected by KM_MODE:
#
#   KM_MODE=keep    (default)  exhaustive KEEP profile  vs  static profile.
#                              Bottoming held IDENTICAL (lookahead, MTG_EXHAUSTIVE_BOTTOM=0) on BOTH,
#                              so the only difference is the mulligan KEEP decision.
#   KM_MODE=bottom             exhaustive keep on BOTH; toggle bottoming: A=lookahead (=0), B=blind
#                              exhaustive (=1). The only difference is the BOTTOMING policy.
#                              Set MTG_CONFOUND_BOTTOM=1 in the environment for the confounded A/B
#                              (reshuffle after the decision -> nullifies the lookahead peek).
#   KM_MODE=versus             KM_EXH_A (old table) vs KM_EXH_B (new table), BOTH in the shipping
#                              condition (exhaustive keep + blind bottoming). The REGENERATION
#                              question. Driven automatically by scripts/mullgen.sh on a regen.
#
# Writes $OUT/delta.txt (mean B-A, turns) alongside the human REPORT.txt, so a caller can gate on it.
#
# Plays with the deck's REAL PLAY PROFILE: the manifest omits "depth" so the deck's enabled value_play
# block owns the play depth (the shipping condition), with budget_ms as the CLI knob. Each arm runs as
# ONE `mtg --batch` over all seeds -> a single pooled work queue (one load-imbalance tail per arm, not
# one per seed -- the old per-seed sweep stranded 23 cores on each config's slow-storm tail). The
# exhaustive keep/bottom policy is layered via MTG_EXHAUSTIVE_PROFILE on top of the base profile's
# value_play; requires the exhaustive profile to already exist (generate via MTG_KEEP_EXHAUSTIVE).
#
#   KM_DECK=decks/<name>/<name>.txt KM_MODE=keep bash test/keepmodel_exhaustive_ab.sh
set -uo pipefail
cd "$(dirname "$0")/.."
BIN=./build/Release/mtg
DECK=${KM_DECK:?set KM_DECK=decks/<name>/<name>.(txt|cod)}
MODE=${KM_MODE:-keep}
STEM=$(basename "$DECK"); STEM=${STEM%.*}
# BASE = the deck's base .profile.json (carries the value_play PLAY profile). EXH = the exhaustive
# keep/bottom sidecar (layered on top via env). Per-deck-folder decks need these passed explicitly.
BASE=${KM_STATIC:-decks/$STEM.profile.json}
EXH=${KM_EXH_PROFILE:-decks/$STEM.keepmodel.exhaustive.profile.json}
[ -e "$EXH" ]  || { echo "missing exhaustive profile: $EXH (generate with MTG_KEEP_EXHAUSTIVE=1)"; exit 1; }
[ -e "$BASE" ] || { echo "missing base/static profile: $BASE"; exit 1; }

SEEDS=${KM_AB_SEEDS:-"4004 5005 6006 7007 8008 9009 10010 11011 12012 13013 14014 15015 16016 17017 18018 19019"}
GAMES=${KM_AB_GAMES:-1000}
BUDGET=${KM_AB_BUDGET:-20}

OUT=logs/keepmodel_exh_${MODE}_$STEM; mkdir -p "$OUT"
REPORT=$OUT/REPORT.txt
stamp(){ date -u +%Y-%m-%dT%H:%M:%SZ; }
log(){ echo "$*" | tee -a "$REPORT"; }
: > "$REPORT"
nseed=$(echo $SEEDS | wc -w)
log "=== EXHAUSTIVE keep/bottom A/B ($(stamp)) deck=$STEM mode=$MODE  [batched, play-profile] ==="
log "seeds=$nseed games=$GAMES budget-ms=$BUDGET (value_play owns depth)  confound=${MTG_CONFOUND_BOTTOM:-0}"
log "exhaustive profile: $EXH"

# One manifest, seeds pooled, PLAY-PROFILE driven (no "depth" key -> value_play owns the depth).
MF=$OUT/manifest.json
{ echo '{ "jobs": ['; first=1
  for s in $SEEDS; do [ $first -eq 1 ] && first=0 || printf ',\n'
    printf '  { "name": "s%s", "deck": "%s", "profile": "%s", "games": %s, "seed": %s, "budget_ms": %s }' \
      "$s" "$DECK" "$BASE" "$GAMES" "$s" "$BUDGET"
  done; printf '\n] }\n'; } > "$MF"

# run_arm <tag> <exhaustive-profile|none> <exhaustive_bottom 0|1> -- ONE pooled batch (inherits any
# MTG_CONFOUND_BOTTOM from the caller's environment).
run_arm(){ local tag="$1" prof="$2" exb="$3"
  MTG_EXHAUSTIVE_PROFILE="$prof" MTG_EXHAUSTIVE_BOTTOM="$exb" \
    "$BIN" --batch "$MF" --threads 0 --game-log-dir "$OUT/wins_$tag" \
    > "$OUT/batch_$tag.log" 2>"$OUT/batch_$tag.err"; }

if [ "$MODE" = keep ]; then
  A_TAG=static;   A_PROF=none;  A_EXB=0     # static keep,      lookahead bottoming
  B_TAG=exh;      B_PROF=$EXH;  B_EXB=0     # exhaustive keep,  lookahead bottoming
elif [ "$MODE" = versus ]; then
  # PROFILE-vs-PROFILE: the REGENERATION question ("is the new table better than the one we ship?"),
  # which neither other mode can ask -- both of those compare a policy against a NON-exhaustive
  # baseline. Both arms run in the SHIPPING condition (exhaustive keep + blind exhaustive bottoming,
  # since generation bakes bottoming_enabled=true), so the ONLY difference is which table is loaded.
  A_TAG=old; A_PROF=${KM_EXH_A:?versus mode needs KM_EXH_A=<old profile>}; A_EXB=1
  B_TAG=new; B_PROF=${KM_EXH_B:?versus mode needs KM_EXH_B=<new profile>}; B_EXB=1
  [ -e "$A_PROF" ] || { echo "missing KM_EXH_A: $A_PROF"; exit 1; }
  [ -e "$B_PROF" ] || { echo "missing KM_EXH_B: $B_PROF"; exit 1; }
  log "versus: A(old)=$A_PROF"
  log "versus: B(new)=$B_PROF"
else
  A_TAG=lookahead; A_PROF=$EXH; A_EXB=0     # exhaustive keep,  lookahead bottoming
  B_TAG=exhbottom; B_PROF=$EXH; B_EXB=1     # exhaustive keep,  blind exhaustive bottoming
fi
log "--- arm A=$A_TAG ($(stamp)) ---"; run_arm "$A_TAG" "$A_PROF" "$A_EXB"
log "--- arm B=$B_TAG ($(stamp)) ---"; run_arm "$B_TAG" "$B_PROF" "$B_EXB"

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

# PER-SEED, always. The mean is the verdict but it is not the whole story: an adopt/reject decision
# needs to see whether a delta is broad or is one outlier seed dragging the average, and a rejected
# profile is exactly when you most need the detail to decide what to do next.
print(f"\n--- per seed ---")
print(f"{'seed':>10}{A:>14}{B:>14}{'delta':>12}")
for s, x, y in sorted(per, key=lambda r: r[2] - r[1]):
    print(f"{s:>10}{x:>14.4f}{y:>14.4f}{y-x:>+12.4f}")
if per:
    ds = sorted(y - x for _, x, y in per)
    n = len(ds)
    med = ds[n // 2] if n % 2 else 0.5 * (ds[n // 2 - 1] + ds[n // 2])
    sd = (sum((d - (mB - mA)) ** 2 for d in ds) / (n - 1)) ** 0.5 if n > 1 else 0.0
    se = sd / (n ** 0.5) if n > 1 else 0.0
    print(f"\nspread: min {ds[0]:+.4f}  median {med:+.4f}  max {ds[-1]:+.4f}"
          f"   sd {sd:.4f}  se {se:.4f}  mean/se {((mB-mA)/se if se else 0):+.2f}")
print(f"\noverall delta {mB-mA:+.4f}t  ({B+' BEATS '+A if mB-mA < -1e-4 else (B+' loses to '+A if mB-mA > 1e-4 else 'tie (within noise)')})")
# MACHINE-READABLE, for scripts/mullgen.sh's gate. The mean delta is the whole verdict: the accept
# bar is "not worse ON AVERAGE" (user, 2026-08-28), so a caller must not have to re-parse prose.
open(f"{OUT}/delta.txt", "w").write(f"{mB-mA:.6f}\n")
PY
log "=== EXHAUSTIVE A/B DONE ($(stamp)) ==="

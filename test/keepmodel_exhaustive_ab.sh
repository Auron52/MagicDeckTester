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
# block owns the play depth (the shipping condition), with budget_ms as the CLI knob. BOTH arms run in
# ONE `mtg --batch` over all seeds -> a single pooled work queue and one tail (the per-job
# "exhaustive_profile" / "exhaustive_bottom" manifest keys carry what used to be the per-arm
# MTG_EXHAUSTIVE_PROFILE / MTG_EXHAUSTIVE_BOTTOM environment, which forced one batch per arm). The
# exhaustive keep/bottom policy is layered on top of the base profile's value_play; requires the
# exhaustive profile to already exist (generate via MTG_KEEP_EXHAUSTIVE).
#
# EARLY STOP (user, 2026-10-08: the new keep table "shouldn't even be close" to the static rules). The
# arms are interleaved seed by seed, so seed PAIRS finish in order; test/keepmodel_early_stop.py is
# polled while the batch runs and ends it as soon as arm B is CLEARLY better (paired t <= -8 over at
# least 4 seed pairs: <= 0.31% chance of stopping when B is not better). One-sided: a B that looks worse
# always runs the full planned sample, because a reject quarantines a live profile. KM_EARLY_STOP=0
# turns it off; KM_EARLY_STOP_T / _MIN_PAIRS / _MIN_ABS tune it (stricter only, please).
#
#   KM_DECK=decks/<name>/<name>.txt KM_MODE=keep bash test/keepmodel_exhaustive_ab.sh
set -uo pipefail
cd "$(dirname "$0")/.."
# PGO+LTO when it matches HEAD:src, else Release -- test/lib/harness.sh (user, 2026-10-08).
. test/lib/harness.sh
BIN=$(harness_bin) || exit 1
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

# KM_OUT lets a caller give each ROUND its own directory, which is what makes escalation poolable:
# the default path is per-(mode,deck), so a second round would otherwise overwrite the first's batch
# logs and there would be nothing left to pool. See scripts/mullgen.sh.
OUT=${KM_OUT:-logs/keepmodel_exh_${MODE}_$STEM}; mkdir -p "$OUT"
REPORT=$OUT/REPORT.txt
stamp(){ date -u +%Y-%m-%dT%H:%M:%SZ; }
log(){ echo "$*" | tee -a "$REPORT"; }
: > "$REPORT"
nseed=$(echo $SEEDS | wc -w)
log "=== EXHAUSTIVE keep/bottom A/B ($(stamp)) deck=$STEM mode=$MODE  [batched, play-profile] ==="
log "seeds=$nseed games=$GAMES budget-ms=$BUDGET (value_play owns depth)  confound=${MTG_CONFOUND_BOTTOM:-0}"
log "exhaustive profile: $EXH"

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

EARLY=${KM_EARLY_STOP:-1}
EARLY_T=${KM_EARLY_STOP_T:-8}
EARLY_MIN=${KM_EARLY_STOP_MIN_PAIRS:-4}
EARLY_ABS=${KM_EARLY_STOP_MIN_ABS:-0}

# One manifest, BOTH arms, interleaved seed by seed (A s1, B s1, A s2, ...), PLAY-PROFILE driven (no
# "depth" key -> value_play owns the depth). Every job shares depth/budget/profile, so the batch's LPT
# sort keeps manifest order and the pairs complete in order -- which is what lets the early stop judge
# a prefix of seeds rather than whichever jobs happened to finish.
jbool(){ [ "$1" = 1 ] && echo true || echo false; }
MF=$OUT/manifest.json
{ echo '{ "jobs": ['; first=1
  for s in $SEEDS; do
    for side in A B; do
      if [ $side = A ]; then tag=$A_TAG prof=$A_PROF exb=$A_EXB; else tag=$B_TAG prof=$B_PROF exb=$B_EXB; fi
      [ $first -eq 1 ] && first=0 || printf ',\n'
      printf '  { "name": "s%s@%s", "deck": "%s", "profile": "%s", "games": %s, "seed": %s, "budget_ms": %s, "exhaustive_profile": "%s", "exhaustive_bottom": %s }' \
        "$s" "$tag" "$DECK" "$BASE" "$GAMES" "$s" "$BUDGET" "$prof" "$(jbool "$exb")"
    done
  done; printf '\n] }\n'; } > "$MF"

# ONE pooled batch for both arms (inherits any MTG_CONFOUND_BOTTOM from the caller's environment, which
# applies to both arms alike). The per-arm env levers are cleared: the manifest carries them now, and
# the batch refuses a job whose manifest key and env form are both set.
log "--- arms A=$A_TAG B=$B_TAG: one pooled batch, seeds interleaved ($(stamp)) ---"
rm -f "$OUT/EARLY_STOP.txt" "$OUT/EARLY_STOP_SEEDS.txt"
env -u MTG_EXHAUSTIVE_PROFILE -u MTG_EXHAUSTIVE_BOTTOM \
  "$BIN" --batch "$MF" --threads 0 --game-log-dir "$OUT/wins" > "$OUT/batch.log" 2> "$OUT/batch.err" &
BPID=$!
if [ "$EARLY" = 1 ]; then
  while kill -0 "$BPID" 2>/dev/null; do
    for _ in 1 2 3 4 5 6; do kill -0 "$BPID" 2>/dev/null || break; sleep 5; done
    if python3 test/keepmodel_early_stop.py "$OUT/batch.log" "$A_TAG" "$B_TAG" "$GAMES" \
         "$EARLY_MIN" "$EARLY_T" "$EARLY_ABS" "$SEEDS" "$OUT/EARLY_STOP_SEEDS.txt" \
         > "$OUT/early_stop.tmp" 2>/dev/null; then
      mv "$OUT/early_stop.tmp" "$OUT/EARLY_STOP.txt"
      kill "$BPID" 2>/dev/null
      log "EARLY STOP ($(stamp)): $(cat "$OUT/EARLY_STOP.txt")"
      break
    fi
  done
  rm -f "$OUT/early_stop.tmp"
fi
wait "$BPID"; brc=$?
[ -e "$OUT/EARLY_STOP.txt" ] || [ "$brc" -eq 0 ] || log "batch exited $brc -- see $OUT/batch.err"
log "--- batch done ($(stamp)) ---"
# Per-arm logs in the per-arm format every reader parses ("s<seed>: played=..."), so the report below
# and scripts/mullgen.sh's round pooling (test/keepmodel_pool_ab.py) are unchanged. After an early stop
# they carry EXACTLY the seeds the stop rule judged (EARLY_STOP_SEEDS.txt), so the reported delta is the
# one the decision was made on; otherwise every completed job.
python3 - "$OUT" "$A_TAG" "$B_TAG" <<'SPLIT'
import os, re, sys
out, tags = sys.argv[1], sys.argv[2:]
sf = os.path.join(out, "EARLY_STOP_SEEDS.txt")
keep = set(open(sf).read().split()) if os.path.exists(sf) else None
lines = open(os.path.join(out, "batch.log")).read().splitlines()
for t in tags:
    with open(os.path.join(out, f"batch_{t}.log"), "w") as w:
        for ln in lines:
            m = re.match(r"s(\d+)@(\S+): (.*)", ln)
            if m and m.group(2) == t and (keep is None or m.group(1) in keep):
                w.write(f"s{m.group(1)}: {m.group(3)}\n")
SPLIT

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

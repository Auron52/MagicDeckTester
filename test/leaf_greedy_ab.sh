#!/usr/bin/env bash
# A/B for LEAF FIDELITY BY PASS DEPTH (MTG_LEAF_GREEDY_D2/_D3/_D4; see LeafGreedyFromDepth in
# src/ai/TurnSolver.cpp): a commit-the-line pass of nominal depth >= K scores its horizon with a GREEDY
# playout instead of the 1-ply lookahead leaf; shallower passes keep the 1-ply leaf. Motivated by Snow's
# value-leaf matrix (docs/design/snow-matrix-leaf-fidelity.md): unbounded, the greedy leaf is identical
# at H3-H5 (186/186 paired games) and 50-460x cheaper on the heavy games, but worse at H1/H2.
#
# EVERY ARM IN ONE POOLED BATCH, paired on the same seeds and game indices. Cells are a suite tier's own
# (test/regression_cases.sh), restricted to depth >= MIN_DEPTH (default 3: a pass shallower than K never
# moves, so lower cells are byte-identical by construction and only cost time). SEED_OFFSET (default
# 900000) moves every cell onto HELD-OUT seeds, disjoint from every GT tier.
#
# Usage:
#   bash test/leaf_greedy_ab.sh                       # smoke-tier cell shapes, held-out seeds, arms base/d3/d4
#   CASES=regression ARMSET=all RUN=<name> bash test/leaf_greedy_ab.sh
#   DECKS=snow,th bash test/leaf_greedy_ab.sh         # restrict to decks (comma list of case keys)
#   DRY_RUN=1 ...                                     # write the manifest only
# Then: python3 test/paired_arms.py logs/leaf_greedy_ab/<run>/wins --base base --arm d3 [--list-moved]
set -uo pipefail
cd "$(dirname "$0")/.."
. test/lib/harness.sh
source test/regression_cases.sh

BIN=${MTG_BIN:-$(harness_bin)} || exit 1
TIER=${CASES:-smoke}
case "$TIER" in
  smoke)      CASES_REF=("${SMOKE_CASES[@]}") ;;
  regression) CASES_REF=("${REGRESSION_CASES[@]}") ;;
  overnight)  CASES_REF=("${OVERNIGHT_CASES[@]}") ;;
  *) echo "CASES must be smoke|regression|overnight" >&2; exit 2 ;;
esac
RUN=${RUN:-$(date +%Y%m%d_%H%M%S)_$TIER}
OUT=logs/leaf_greedy_ab/$RUN
mkdir -p "$OUT/wins"
ONLY=${DECKS:-}
python3 - "$OUT/manifest.json" "$ONLY" "${CASES_REF[@]}" <<'PY'
import json, sys, os, re
out, only = sys.argv[1], sys.argv[2]
cases = sys.argv[3:]
only = set(only.split(",")) if only else None
# DECK_FILE / DECK_PROF are bash assoc arrays; re-read them from the cases file.
src = open("test/regression_cases.sh").read()
def amap(name):
    m = re.search(r"declare -A %s=\((.*?)\n\)" % name, src, re.S)
    d = {}
    for k, v in re.findall(r'^\s*\[([a-z0-9_]+)\]=(.+)$', m.group(1), re.M):
        d[k] = v.strip().strip('"')
    return d
FILE, PROF = amap("DECK_FILE"), amap("DECK_PROF")
ARMSETS = {
    "default": {"base": {}, "d3": {"MTG_LEAF_GREEDY_D3": True}, "d4": {"MTG_LEAF_GREEDY_D4": True}},
    "all":     {"base": {}, "d2": {"MTG_LEAF_GREEDY_D2": True}, "d3": {"MTG_LEAF_GREEDY_D3": True},
                "d4": {"MTG_LEAF_GREEDY_D4": True}},
}
_UNUSED = {
    # the two tap-order levers, singly and jointly
    "levers": {
        "base":   {"MTG_NEEDS_TAP_ORDER": False, "MTG_ATTACK_BODY_TAP_ORDER": False},
        "needs":  {"MTG_NEEDS_TAP_ORDER": True,  "MTG_ATTACK_BODY_TAP_ORDER": False},
        "attack": {"MTG_NEEDS_TAP_ORDER": False, "MTG_ATTACK_BODY_TAP_ORDER": True},
        "both":   {"MTG_NEEDS_TAP_ORDER": True,  "MTG_ATTACK_BODY_TAP_ORDER": True},
    },
    # the mana-payment ROLLBACK (src/ai/PayRollback.h), alone and on top of both levers
    "rollback": {
        "base":     {"MTG_NEEDS_TAP_ORDER": False, "MTG_ATTACK_BODY_TAP_ORDER": False, "MTG_PAY_ROLLBACK": False},
        "rollback": {"MTG_NEEDS_TAP_ORDER": False, "MTG_ATTACK_BODY_TAP_ORDER": False, "MTG_PAY_ROLLBACK": True},
        "all3":     {"MTG_NEEDS_TAP_ORDER": True,  "MTG_ATTACK_BODY_TAP_ORDER": True,  "MTG_PAY_ROLLBACK": True},
    },
    # every arm in ONE pool: the two levers singly and jointly, the rollback alone, all three
    "all": {
        "base":     {"MTG_NEEDS_TAP_ORDER": False, "MTG_ATTACK_BODY_TAP_ORDER": False, "MTG_PAY_ROLLBACK": False},
        "needs":    {"MTG_NEEDS_TAP_ORDER": True,  "MTG_ATTACK_BODY_TAP_ORDER": False, "MTG_PAY_ROLLBACK": False},
        "attack":   {"MTG_NEEDS_TAP_ORDER": False, "MTG_ATTACK_BODY_TAP_ORDER": True,  "MTG_PAY_ROLLBACK": False},
        "both":     {"MTG_NEEDS_TAP_ORDER": True,  "MTG_ATTACK_BODY_TAP_ORDER": True,  "MTG_PAY_ROLLBACK": False},
        "rollback": {"MTG_NEEDS_TAP_ORDER": False, "MTG_ATTACK_BODY_TAP_ORDER": False, "MTG_PAY_ROLLBACK": True},
        "all3":     {"MTG_NEEDS_TAP_ORDER": True,  "MTG_ATTACK_BODY_TAP_ORDER": True,  "MTG_PAY_ROLLBACK": True},
    },
}
ARMS = ARMSETS[os.environ.get("ARMSET", "default")]
OFF = int(os.environ.get("SEED_OFFSET", "900000"))
MIN_DEPTH = int(os.environ.get("MIN_DEPTH", "3"))
jobs = []
for spec in cases:
    deck, depth, seed, games, budget = spec.split()
    depth, seed, games, budget = int(depth), int(seed) + OFF, int(games), int(budget)
    if depth < MIN_DEPTH: continue
    base = deck[:-3] if deck.endswith("2hg") else deck
    if only and deck not in only and base not in only: continue
    extra = {"starting_life": 30, "opponent_heads": 2} if deck.endswith("2hg") else {}
    bud = budget if depth > 0 else 0
    weight = (depth * 1000 + bud) if (base == "hinata" and depth > 0) else 0
    for arm, flags in ARMS.items():
        # paired_arms.py splits the .wins name on "__": everything before is the row key.
        j = {"name": f"{deck}_d{depth}_s{seed}__{arm}", "deck": FILE[base], "profile": PROF[base],
             **extra, "games": games, "seed": seed, "budget_ms": bud, "weight": weight}
        if flags: j["flags"] = flags
        if depth != 5:   # d5 cells drop the depth key so value_play owns it (regression.sh's rule)
            j["depth"] = depth; j["ignore_play_profile"] = True
        jobs.append(j)
json.dump({"jobs": jobs}, open(out, "w"), indent=1)
print(f"{len(jobs)} jobs, {sum(j['games'] for j in jobs)} games ({len(ARMS)} arms)")
PY

[[ -n "${DRY_RUN:-}" ]] && { echo "DRY_RUN: manifest at $OUT/manifest.json"; exit 0; }
echo "--- running $TIER cells x $(python3 -c "import json;print(len({j['name'].split('__')[1] for j in json.load(open('$OUT/manifest.json'))['jobs']}))") arms on $BIN -> $OUT ---"
MTG_DUMP_UNITS=1 "$BIN" --batch "$OUT/manifest.json" --threads "${THREADS:-0}" --game-log-dir "$OUT/wins" \
    >"$OUT/batch.out" 2>"$OUT/batch.err"
rc=$?
echo "batch rc=$rc"
grep -E "heartbeat|SLOW-GAME" "$OUT/batch.err" | tail -8
ARMLIST=$(python3 -c "import json,sys; m=json.load(open('$OUT/manifest.json'))['jobs']; print(' '.join(sorted({j['name'].split('__')[1] for j in m}-{'base'})))")
for arm in $ARMLIST; do
  echo; echo "=== $arm vs base ==="
  python3 test/paired_arms.py "$OUT/wins" --base base --arm "$arm"
done | tee "$OUT/paired.txt"

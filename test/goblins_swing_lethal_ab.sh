#!/usr/bin/env bash
# A/B the two Goblins tutor-ranking fixes, cumulatively (see
# docs/design/goblins-tutor-w4-heldout-cost.md).
#
#   arm base          = both levers off  -> the shipped ranking (must reproduce ground truth exactly)
#   arm swing         = + MTG_GOBLIN_SWING_LETHAL: lethal reach vs the swing we will actually make
#   arm swing_enabler = + MTG_GOBLIN_ENABLER_RANK: credit what a fetch UNLOCKS in hand, not just its body
#
# Both arms use the SAME binary (the lever is an env flag), so this isolates the change with no
# rebuild and no risk of two identical arms (regression-testing skill, rules 5-6).
#
# Every Goblins case of ALL THREE modes goes into ONE manifest per arm, so the run pays a single
# load-imbalance tail rather than one per case (CLAUDE.md: pool into one batch, never a loop of
# small invocations). smoke+regression seeds are TRAIN; overnight seeds are HELD-OUT -- and note
# the fix was derived from an overnight game (gi865), so overnight is only partly held out.
#
# Usage: bash test/goblins_swing_lethal_ab.sh [--threads N]
set -u
cd "$(dirname "$0")/.."
THREADS=${THREADS:-$(nproc)}
[ "${1-}" = "--threads" ] && THREADS=$2
BIN=build/Release/mtg
OUT=test/logs/goblins_swing_ab
mkdir -p "$OUT"

python3 - "$OUT/manifest.json" <<'PY'
import re, sys, json
txt = open("test/regression_cases.sh").read()
def amap(n):
    m = re.search(r"declare -A " + n + r"=\((.*?)\)", txt, re.S)
    return dict(re.findall(r"\[(\w+)\]=(\S+)", m.group(1))) if m else {}
def arr(n):
    m = re.search(n + r"=\((.*?)\n\)", txt, re.S)
    return re.findall(r'"([^"]+)"', m.group(1)) if m else []
FILE, PROF = amap("DECK_FILE"), amap("DECK_PROF")
jobs = []
for mode, name in (("smoke", "SMOKE_CASES"), ("regression", "REGRESSION_CASES"),
                   ("overnight", "OVERNIGHT_CASES")):
    for spec in arr(name):
        deck, depth, seed, games, budget = spec.split()
        if deck != "goblins":
            continue
        depth, budget = int(depth), int(budget)
        bud = budget if depth > 0 else 0
        j = {"name": f"{deck}_{mode}_d{depth}_s{seed}", "deck": FILE[deck],
             "profile": PROF[deck], "games": int(games), "seed": int(seed),
             "budget_ms": bud, "weight": 0}
        # Mirror regression.sh exactly: d5 drops `depth` so the deck's value_play block owns it;
        # d0/d3 pin depth and must bypass that block. Getting this wrong makes the numbers
        # incomparable to ground truth.
        if depth != 5:
            j["depth"] = depth
            j["ignore_play_profile"] = True
        jobs.append(j)
json.dump({"jobs": jobs}, open(sys.argv[1], "w"), indent=1)
print(f"{len(jobs)} goblins jobs, {sum(j['games'] for j in jobs)} games per arm")
PY

# Three arms, cumulative: each adds one lever on top of the previous, so the report reads as the
# marginal contribution of each. `base` MUST reproduce committed ground truth exactly (avg AND
# digest) -- that is the A/B validity check (regression-testing skill, rule 6).
for arm in base swing swing_enabler; do
  echo "=== arm=$arm ($(date +%H:%M:%S)) ==="
  rm -rf "$OUT/wins_$arm"; mkdir -p "$OUT/wins_$arm"
  case $arm in
    base)          export MTG_GOBLIN_SWING_LETHAL=0 MTG_GOBLIN_ENABLER_RANK=0 ;;
    swing)         unset MTG_GOBLIN_SWING_LETHAL;  export MTG_GOBLIN_ENABLER_RANK=0 ;;
    swing_enabler) unset MTG_GOBLIN_SWING_LETHAL MTG_GOBLIN_ENABLER_RANK ;;
  esac
  "$BIN" --batch "$OUT/manifest.json" --threads "$THREADS" \
      --game-log-dir "$OUT/wins_$arm" 2>"$OUT/$arm.err" | tee "$OUT/$arm.log"
done

python3 - "$OUT" <<'PY'
import sys, os, re, glob
out = sys.argv[1]
MAXT = {0: 10, 3: 10, 5: 10}   # unwon sentinel handled below from the .wins -1 convention


def parse(log):
    d = {}
    for line in open(log):
        m = re.match(r"^(\S+): .*?avg=([0-9.]+).*?digest=([0-9a-f]+)", line)
        if m:
            d[m.group(1)] = (float(m.group(2)), m.group(3))
    return d


ARMS = ["base", "swing", "swing_enabler"]
res = {a: parse(f"{out}/{a}.log") for a in ARMS}
off, on = res["base"], res["swing_enabler"]


def wins(arm, key):
    p = f"{out}/wins_{arm}/{key}.wins"
    if not os.path.exists(p):
        return {}
    r = {}
    for line in open(p):
        f = line.split()
        if len(f) >= 2:
            r[int(f[0])] = int(f[1])
    return r


print(f"\n{'case':<34} {'base':>8} {'swing':>8} {'+enabl':>8} {'delta':>9}  {'games':>6}  slower/faster")
tot = {}
for key in sorted(off):
    a, b = off[key][0], on[key][0]
    wa, wb = wins("base", key), wins("swing_enabler", key)
    n = len(wa)
    # .wins uses -1 for an unwon game; the harness metric scores it max_turns+1.
    maxt = max([v for v in list(wa.values()) + list(wb.values()) if v > 0] + [0])
    pen = lambda v: (maxt + 1) if v < 0 else v
    sl = sum(1 for g in wa if g in wb and pen(wb[g]) > pen(wa[g]))
    fa = sum(1 for g in wa if g in wb and pen(wb[g]) < pen(wa[g]))
    tier = "train" if ("_smoke_" in key or "_regression_" in key) else "heldout"
    depth = int(re.search(r"_d(\d+)_", key).group(1))
    grp = (tier, "d0" if depth == 0 else "searched")
    for arm in ARMS:
        tot.setdefault((arm, grp), [0, 0.0, 0, 0])
        va = res[arm][key][0]
        wx = wins(arm, key)
        s2 = sum(1 for g in wa if g in wx and pen(wx[g]) > pen(wa[g]))
        f2 = sum(1 for g in wa if g in wx and pen(wx[g]) < pen(wa[g]))
        e = tot[(arm, grp)]
        e[0] += n; e[1] += (va - a) * n; e[2] += s2; e[3] += f2
    mid = res["swing"][key][0]
    flag = "" if abs(b - a) < 1e-9 else ("  <-- BETTER" if b < a else "  <-- WORSE")
    print(f"{key:<34} {a:8.4f} {mid:8.4f} {b:8.4f} {b-a:+9.4f}  {n:6d}  {sl:3d}/{fa:<3d}{flag}")

print(f"\n{'arm / group':<34} {'games':>7} {'weighted delta':>15} {'turn-units':>11}  slower/faster")
for arm in ARMS:
    for grp in sorted(g for (a, g) in tot if a == arm):
        n, s, sl, fa = tot[(arm, grp)]
        print(f"{arm + ' / ' + '/'.join(grp):<34} {n:7d} {s/n:+15.4f} {s:+11.1f}  {sl:3d}/{fa:<3d}")
    print()
print("\n(negative delta = faster = better; primary metric = loss-penalized avg win turn)")
PY

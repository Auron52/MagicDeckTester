#!/bin/bash
# Hands-off Sunday pipeline: MTG_NC_TOPM policy-prior sweep across all 5 decks (clean CPU, one at a
# time so wall-time is honest), then the antilife d5 depth re-run (LP only, runs alone at the end).
# Outputs under logs/model_improve/. Slivers last (heavy tail may hit the 30-min/batch timeout).
set -u
cd /workspaces/MagicDeckTester2
OUT=logs/model_improve
mkdir -p "$OUT"
SEEDS="4001 4002 9009 2002 3003 7007"
TOPM="0 8 4 2 1"

echo "[1/6] antilife (dyn)  $(date +%H:%M)"
python3 scripts/nc_topm_sweep.py --deck decks/Anti-Lifegain.cod --dyn logs/eval/antilife_teacherd2_dyn.json \
    --max-turns 10 --seeds $SEEDS --topm $TOPM > "$OUT/topm_antilife.out" 2>&1

echo "[2/6] TH (dyn)  $(date +%H:%M)"
python3 scripts/nc_topm_sweep.py --deck decks/treasure_hunt.txt --dyn logs/eval/TH_teacherd2_dyn.json \
    --max-turns 8 --seeds $SEEDS --topm $TOPM > "$OUT/topm_TH.out" 2>&1

echo "[3/6] burn (value)  $(date +%H:%M)"
python3 scripts/nc_topm_sweep.py --deck decks/burn.txt --value decks/burn.value.json \
    --max-turns 8 --seeds $SEEDS --topm $TOPM > "$OUT/topm_burn.out" 2>&1

echo "[4/6] knights (value)  $(date +%H:%M)"
python3 scripts/nc_topm_sweep.py --deck decks/Knights.cod --value decks/Knights.value.json \
    --max-turns 8 --seeds $SEEDS --topm $TOPM > "$OUT/topm_knights.out" 2>&1

echo "[5/6] slivers (value)  $(date +%H:%M)"
python3 scripts/nc_topm_sweep.py --deck decks/slivers_vial.txt --value decks/slivers_vial.value.json \
    --max-turns 8 --seeds $SEEDS --topm $TOPM > "$OUT/topm_slivers.out" 2>&1

echo "[6/6] antilife d5 depth re-run  $(date +%H:%M)"
python3 - > "$OUT/d5_rerun.out" 2>&1 <<'PY'
import os, re, subprocess, time
def run(k, d, seeds, games, mt):
    env = {x: v for x, v in os.environ.items() if not x.startswith("MTG_")}
    env.update({"MTG_NC_SEARCH": "1", "MTG_NC_K": str(k), "MTG_NC_DEPTH": str(d)})
    lps = []; t0 = time.time()
    for s in seeds:
        out = subprocess.run(["build/Release/mtg", "decks/Anti-Lifegain.cod", "--games", str(games),
            "--seed", str(s), "--depth", "1", "--max-turns", str(mt), "--threads", "12"],
            capture_output=True, text=True, env=env).stdout
        p = int(re.search(r'played\s*:\s*(\d+)', out).group(1)); w = int(re.search(r'won\s*:\s*(\d+)', out).group(1))
        a = float(re.search(r'Avg win turn\s*:\s*([\d.]+)', out).group(1))
        lps.append((w * a + (p - w) * (mt + 1)) / p)
    return sum(lps) / len(lps), time.time() - t0
seeds = [4001, 4002, 9009]; games = 40
for d in [2, 3, 5]:
    lp, dt = run(16, d, seeds, games, 10)
    print(f"NC-K16-d{d}: LP={lp:.3f}  ({games*len(seeds)} games, {dt:.0f}s)", flush=True)
PY

echo "=== ALL DONE  $(date +%H:%M) ==="

#!/usr/bin/env python3
"""Attribute each changed deck's play shift to CROSSOVER (metadata) vs SRC (uncommitted runtime), then A/B the
crossover-caused decks on TRAINED vs HELD-OUT seeds.

Attribution per (deck,depth,seed) regression case:
  default  = per-depth table crossover (what ships)
  offset3  = MTG_VALUE_TRUST_OFFSET=3 -> forced legacy uniform rule (ignores the table crossover)
  GT       = committed ground-truth avg (passed in)
  * offset3 == GT      => the change is the CROSSOVER (uniform reproduces GT).
  * offset3 == default != GT => the change is the SRC (crossover inert; uncommitted runtime moved it).

Then for CROSSOVER-caused decks, paired A/B per-depth vs uniform on trained + held-out seeds:
  dLP = per-depth - uniform ; NEGATIVE => crossover better."""
import os, re, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import valueleaf_depth_matrix as vm

def run(deck_path, depth, seed, games, budget, threads, off):
    env = dict(os.environ)
    for k in ("MTG_VALUE_MODEL","MTG_VALUE_PROFILE","MTG_VALUE_MIN_DEPTH","MTG_VALUE_STARTGATE_ALPHA","MTG_VALUE_TRUST_OFFSET"):
        env.pop(k, None)
    if off is not None: env["MTG_VALUE_TRUST_OFFSET"] = str(off)
    cmd = [vm.MTG, deck_path, "--depth", str(depth), "--seed", str(seed), "--games", str(games),
           "--max-turns", "8", "--budget-ms", str(budget), "--threads", str(threads)]
    out = subprocess.run(cmd, capture_output=True, text=True, env=env).stdout
    p = int(re.search(r"Games played\s*:\s*(\d+)", out).group(1))
    w = int(re.search(r"Games won\s*:\s*(\d+)", out).group(1))
    m = re.search(r"Avg win turn\s*:\s*([\d.]+)", out); a = float(m.group(1)) if m else float("nan")
    return p, w, a, (w*a + (p-w)*9)/p

TH_TRACE = 16  # threads per run (box free -> use it)

# (deck, depth, seed, games, budget, GT_avg) -- one representative regression case each
ATTR = [
    ("slivers", 3, 3003, 400, 10, 4.2450),
    ("TH",      5, 2002, 300, 20, 4.11037),
    ("antilife",5, 2002, 250, 20, 4.63115),
    ("hinata",  3, 2002, 200, 10, 5.81714),
]
print("=========== ATTRIBUTION: crossover vs src ===========")
crossover_decks = []
for deck, depth, seed, games, budget, gt in ATTR:
    dp = vm.DECKS[deck][0]
    _,_,aD,_ = run(dp, depth, seed, games, budget, TH_TRACE, None)
    _,_,aU,_ = run(dp, depth, seed, games, budget, TH_TRACE, 3)
    if abs(aU - gt) < 1e-4:      cause = "CROSSOVER (uniform==GT)"; crossover_decks.append(deck)
    elif abs(aU - aD) < 1e-4:    cause = "SRC (uniform==default!=GT)"
    else:                        cause = "MIXED"
    print(f"  {deck:9} d{depth} s{seed}: default={aD:.5f} uniform={aU:.5f} GT={gt:.5f}  -> {cause}")

print("\n=========== TRAINED vs HELD-OUT A/B (crossover-caused decks) ===========")
TRAINED = [8008, 9009, 10010, 11011]
HELDOUT = [40000, 55000, 70000]
# depth/games/budget to sweep per crossover deck
SWEEP = {"slivers":[(3,3000,10),(5,2000,20)], "antilife":[(5,2000,20)], "hinata":[(3,1500,10)]}
for deck in crossover_decks:
    dp = vm.DECKS[deck][0]
    for depth, games, budget in SWEEP.get(deck, []):
        for label, seeds in (("TRAINED", TRAINED), ("HELDOUT", HELDOUT)):
            tp=tw=0; slpD=slpU=0.0
            for s in seeds:
                p,w,a,lpD = run(dp, depth, s, games, budget, TH_TRACE, None)
                _,_,_,lpU = run(dp, depth, s, games, budget, TH_TRACE, 3)
                tp+=p; slpD+=lpD*p; slpU+=lpU*p
            LPd=slpD/tp; LPu=slpU/tp
            print(f"  {deck:9} d{depth} {label:7}: per-depth LP={LPd:.5f}  uniform LP={LPu:.5f}  "
                  f"dLP={LPd-LPu:+.5f}  ({'per-depth BETTER' if LPd<LPu-1e-5 else 'per-depth WORSE' if LPd>LPu+1e-5 else 'tie'})  over {tp} games")
print("\nDONE")

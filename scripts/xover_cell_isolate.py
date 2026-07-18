#!/usr/bin/env python3
"""Isolate WHICH crossover cell(s) actually need an override for slivers, by patching individual
take_heuristic_at_hdepth entries (the derived POLICY, not the measured table) and measuring play LP.

Base slivers crossover (by committed depth 1..8): [1,1,2,3,3,9,9,9]; uniform effective = [1,1,1,2,3,4,5,6].
In-play divergences at depth<=5: c=3 (2 vs 1), c=4 (3 vs 2). We test:
  perdepth : base (no override)
  c3uni    : override c3 -> 1 (uniform)          -- does removing ONLY c3 recover d3 and help d5?
  c4uni    : override c4 -> 2 (uniform)          -- is c4 per-depth the harmful one? (expect: NO)
  bothuni  : override c3->1, c4->2               -- full in-play uniform via metadata (sanity vs env)
Baseline uniform is also measured via MTG_VALUE_TRUST_OFFSET=3.

Restores the original value.json in a finally. NEVER touches value_leaf_table (the measured data)."""
import json, os, re, shutil, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import valueleaf_depth_matrix as vm

VJSON = "decks/slivers_vial/slivers_vial.value.json"
DECK  = vm.DECKS["slivers"][0]
THREADS = 16

def measure(depth, seed, games, budget, uniform_env=False):
    env = dict(os.environ)
    for k in ("MTG_VALUE_MODEL","MTG_VALUE_PROFILE","MTG_VALUE_MIN_DEPTH","MTG_VALUE_STARTGATE_ALPHA","MTG_VALUE_TRUST_OFFSET"):
        env.pop(k, None)
    if uniform_env: env["MTG_VALUE_TRUST_OFFSET"] = "3"
    cmd = [vm.MTG, DECK, "--depth", str(depth), "--seed", str(seed), "--games", str(games),
           "--max-turns", "8", "--budget-ms", str(budget), "--threads", str(THREADS)]
    out = subprocess.run(cmd, capture_output=True, text=True, env=env).stdout
    p = int(re.search(r"Games played\s*:\s*(\d+)", out).group(1))
    w = int(re.search(r"Games won\s*:\s*(\d+)", out).group(1))
    m = re.search(r"Avg win turn\s*:\s*([\d.]+)", out); a = float(m.group(1)) if m else float("nan")
    return p, (w*a + (p-w)*9)/p

def set_crossover(take_list):
    d = json.load(open(VJSON))
    d["value_fallback_crossover"]["take_heuristic_at_hdepth"] = take_list
    json.dump(d, open(VJSON, "w"))

BASE = [1,1,2,3,3,9,9,9]  # committed 1..8
CONFIGS = {
    "perdepth": BASE,
    "c3uni":    [1,1,1,3,3,9,9,9],
    "c4uni":    [1,1,2,2,3,9,9,9],
    "bothuni":  [1,1,1,2,3,9,9,9],
}
SEEDS = {"TRAINED":[8008,9009], "HELDOUT":[40000,55000]}
DEPTHS = [(3,4000,10),(5,3000,20)]

def agg(cfg_take, uniform_env=False):
    res = {}
    if cfg_take is not None: set_crossover(cfg_take)
    for depth, games, budget in DEPTHS:
        for label, seeds in SEEDS.items():
            tp=0; slp=0.0
            for s in seeds:
                p,lp = measure(depth, s, games, budget, uniform_env)
                tp+=p; slp+=lp*p
            res[(depth,label)] = slp/tp
    return res

def main():
    backup = VJSON + ".bak_isolate"
    shutil.copy(VJSON, backup)
    try:
        print("Measuring uniform baseline (env offset=3)...")
        uni = agg(None, uniform_env=True)   # crossover value irrelevant when env forces uniform
        results = {"uniform(env)": uni}
        for name, take in CONFIGS.items():
            print(f"Measuring {name} = {take[:5]}...")
            results[name] = agg(take)
        print("\n===== slivers crossover-cell isolation (LP; lower=better; vs uniform baseline) =====")
        hdr = "config".ljust(14) + "".join(f"{('d%d/%s'%(d,l)):>16}" for d,l in [(3,'TRAINED'),(3,'HELDOUT'),(5,'TRAINED'),(5,'HELDOUT')])
        print(hdr)
        order = ["uniform(env)","perdepth","c3uni","c4uni","bothuni"]
        for name in order:
            r = results[name]
            row = name.ljust(14)
            for d,l in [(3,'TRAINED'),(3,'HELDOUT'),(5,'TRAINED'),(5,'HELDOUT')]:
                lp = r[(d,l)]; du = lp - uni[(d,l)]
                row += f"{lp:.5f}({du:+.4f})".rjust(16)
            print(row)
        print("\nRead: dLP vs uniform in parens. ~0 = matches uniform; + = worse; - = better.")
    finally:
        shutil.copy(backup, VJSON); os.remove(backup)
        print("\n[restored original", VJSON, "]")

if __name__ == "__main__":
    main()

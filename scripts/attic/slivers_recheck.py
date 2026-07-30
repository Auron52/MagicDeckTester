#!/usr/bin/env python3
"""Re-check the slivers c=3 crossover override IN ISOLATION (fixed binary, fresh-budget OFF).

The override (c3: 2->1) was validated on the WORKING/DRIFT binary (fresh-budget ON), where it showed a
+0.0004 LP "regression" of the DERIVED c3=2 vs uniform. Since antilife's similar single-seed "regression"
turned out to be a wash across seeds once the fresh-budget confound was removed, re-measure slivers the same
way: DERIVED c3=2 (temporarily restored) vs UNIFORM committed-3, paired across many seeds, at d3 (where the
override mattered) and d5.  Backs up the metadata and restores it in a finally block so it can't be left edited.
"""
import json, collections, shutil, subprocess, re, os
from concurrent.futures import ThreadPoolExecutor, as_completed

F = "decks/slivers_vial/slivers_vial.value.json"
BK = "/tmp/slivers_backup.value.json"
BIN = "build_xover/Release/mtg"
DECK = "decks/slivers_vial/slivers_vial.txt"
AVG = re.compile(r"Avg win turn : ([0-9.]+)")
LOG = "logs/eval/slivers_recheck.log"
SEEDS = [2002, 3003, 4004, 5005, 6006, 7007, 10010, 11011, 12012, 13013,
         14014, 15015, 16016, 17017, 18018, 19019]

def say(s):
    print(s, flush=True); open(LOG, "a").write(s + "\n")

def runone(seed, depth, uniform):
    env = dict(os.environ)
    if uniform: env["MTG_VALUE_TRUST_OFFSET"] = "3"
    else: env.pop("MTG_VALUE_TRUST_OFFSET", None)
    g = 400 if depth == 3 else 300
    out = subprocess.run([BIN, DECK, "--depth", str(depth), "--seed", str(seed), "--games", str(g),
                          "--max-turns", "8", "--budget-ms", "20", "--threads", "1"],
                         capture_output=True, text=True, env=env)
    m = AVG.search(out.stdout); return float(m.group(1)) if m else None

def sweep(depth):
    res = {}
    with ThreadPoolExecutor(max_workers=6) as ex:
        futs = {ex.submit(runone, s, depth, u): (s, u) for s in SEEDS for u in (False, True)}
        for fut in as_completed(futs):
            res[futs[fut]] = fut.result()
    rows = [(s, res[(s, False)], res[(s, True)]) for s in SEEDS if res.get((s, False)) is not None and res.get((s, True)) is not None]
    deltas = [round(de - un, 5) for _, de, un in rows]
    md = sum(deltas) / len(deltas) if deltas else 0.0
    worse = sum(1 for x in deltas if x > 1e-9); better = sum(1 for x in deltas if x < -1e-9); same = sum(1 for x in deltas if abs(x) <= 1e-9)
    say(f"### slivers d{depth}: mean DERIVED(c3=2)={sum(r[1] for r in rows)/len(rows):.5f}  uniform={sum(r[2] for r in rows)/len(rows):.5f}  "
        f"mean delta={md:+.5f}  (worse:{worse} better:{better} same:{same} of {len(rows)})")
    for s, de, un in rows:
        dl = round(de - un, 5); flag = "" if abs(dl) <= 1e-9 else ("  <<< WORSE" if dl > 0 else "  <<< better")
        say(f"    s{s}: derived={de:.5f} uniform={un:.5f} delta={dl:+.5f}{flag}")

def main():
    open(LOG, "w").write("")
    shutil.copy(F, BK)
    try:
        p = json.load(open(F), object_pairs_hook=collections.OrderedDict)
        xo = p["value_fallback_crossover"]; v = list(xo["take_heuristic_at_hdepth"]); v[2] = 2  # c=3 -> DERIVED
        xo["take_heuristic_at_hdepth"] = v
        json.dump(p, open(F, "w"), indent=2)
        say(f"=== slivers c=3 RE-CHECK in isolation (DERIVED c3=2 vs uniform, fresh OFF).  effective take_at now {v} ===")
        sweep(3)
        sweep(5)
        say("=== DONE ===")
    finally:
        shutil.copy(BK, F)
        ok = open(BK).read() == open(F).read()
        say(f"[restore] slivers metadata restored from backup: {'OK (byte-identical)' if ok else 'MISMATCH!!'}")

if __name__ == "__main__":
    main()

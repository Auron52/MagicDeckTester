#!/usr/bin/env python3
"""Why does the ladder's d5/b20 cell read 4.520 in wk_firing_evidence.sh but 4.490 in
wk_leaf_vs_leafless.py, on the SAME seed base (4401) and the SAME 300 games?

Two candidate causes, and they have opposite consequences for the ledger:

  A. SIDECAR RESOLUTION. The firing harness points at the real deck dir; the comparison points at a
     scratch dir with symlinked siblings and a copied value.json. If the harness were somehow not
     resolving the sidecar, its win-turn columns would be measuring a deck we do not ship -- the same
     class of defect as the profile-less measurement fixed in that harness on 2026-09-26, and the
     value-leaf skill's documented trap (a missing model silently costs 1.35-84.8x).

  B. MAX_TURNS. The comparison pins `max_turns: 8`; the harness's ladder jobs omit it and take the
     engine default. max_turns is not merely a scoring cutoff -- it bounds the horizon the search
     projects to, so two runs at different max_turns play DIFFERENT games. If this is the cause then
     the harness table is correct as written and it is the 4.490 figure that does not belong in it.

Crossing (deck dir) x (max_turns) separates them in one pooled batch: if the split follows max_turns
the answer is B, if it follows the deck dir the answer is A.
"""
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REAL = os.path.join(ROOT, "decks/WhiteKnights")
SCRATCH = os.path.join(ROOT, "logs/wk_leafcmp_lad/decks/leaf")
OUT = os.path.join(ROOT, "logs/wk_ladiso")


def main():
    if not os.path.exists(os.path.join(SCRATCH, "WhiteKnights.value.json")):
        raise SystemExit(f"missing scratch leaf arm at {SCRATCH} -- run wk_leaf_vs_leafless.py first")
    os.makedirs(OUT, exist_ok=True)
    gl = os.path.join(OUT, "gl")
    subprocess.run(["rm", "-rf", gl], check=True)
    os.makedirs(gl)

    jobs = []
    for dlabel, ddir in (("real", REAL), ("scratch", SCRATCH)):
        for mlabel, mt in (("mtDEFAULT", None), ("mt8", 8)):
            j = {"name": f"{dlabel}_{mlabel}",
                 "deck": os.path.join(ddir, "WhiteKnights.cod"),
                 "profile": os.path.join(ddir, "WhiteKnights.profile.json"),
                 "games": 300, "seed": 4401, "depth": 5, "budget_ms": 20}
            if mt is not None:
                j["max_turns"] = mt
            jobs.append(j)
    mf = os.path.join(OUT, "manifest.json")
    json.dump({"jobs": jobs}, open(mf, "w"), indent=1)

    p = subprocess.run([os.path.join(ROOT, "build/Release/mtg"), "--batch", mf,
                        "--game-log-dir", gl], cwd=ROOT,
                       capture_output=True, text=True)
    if p.returncode != 0:
        sys.stderr.write(p.stderr[-4000:])
        raise SystemExit(f"batch failed rc={p.returncode}")

    def wins(name):
        out = {}
        for line in open(os.path.join(gl, name + ".wins")):
            f = line.split()
            out[int(f[0])] = (int(f[1]), f[2])
        return out

    res = {j["name"]: wins(j["name"]) for j in jobs}
    print(f"\n   {'arm':<20} {'avg win turn':>13} {'slowest':>8} {'n':>5}")
    for n, w in res.items():
        wt = [v[0] for v in w.values()]
        print(f"   {n:<20} {sum(wt)/len(wt):>13.4f} {max(wt):>8} {len(wt):>5}")

    print("\n   pairwise digest identity (are these the same games played the same way?)")
    names = list(res)
    for i in range(len(names)):
        for k in range(i + 1, len(names)):
            a, b = res[names[i]], res[names[k]]
            ks = sorted(set(a) & set(b))
            ident = sum(1 for x in ks if a[x][1] == b[x][1])
            same_wt = sum(1 for x in ks if a[x][0] == b[x][0])
            print(f"   {names[i]:<20} vs {names[k]:<20} digest {ident}/{len(ks)}   win-turn {same_wt}/{len(ks)}")

    print("\n   VERDICT")
    by_mt = (res["real_mt8"], res["scratch_mt8"], res["real_mtDEFAULT"], res["scratch_mtDEFAULT"])
    mt8_same = all(by_mt[0][k][1] == by_mt[1][k][1] for k in by_mt[0])
    dflt_same = all(by_mt[2][k][1] == by_mt[3][k][1] for k in by_mt[2])
    across = all(by_mt[0][k][1] == by_mt[2][k][1] for k in by_mt[0])
    if mt8_same and dflt_same and not across:
        print("   B -- the split follows MAX_TURNS, not the deck dir. The real and scratch dirs play")
        print("   IDENTICALLY at each max_turns, so the harness resolves the sidecar correctly and its")
        print("   ladder table is right as written. max_turns bounds the projected horizon, so 4.490")
        print("   (measured at max_turns 8) is a DIFFERENT measurement, not a refresh of 4.520.")
    elif across and not (mt8_same and dflt_same):
        print("   A -- the split follows the DECK DIR. The firing harness is not seeing the shipped")
        print("   sidecar and every win-turn column it prints is a deck we do not ship.")
    else:
        print("   Neither clean split -- both factors move it; read the table above directly.")


if __name__ == "__main__":
    main()

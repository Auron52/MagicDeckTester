#!/usr/bin/env python3
"""Depth-mismatch test (the "does the value-leaf win only via a DEPTH advantage?" question).

All UNBOUNDED (budget 0) so each config reaches its nominal depth with no budget confound. Per deck/seed:
  H3,H4,H5 = heuristic leaf (value OFF) at depth 3/4/5
  V5       = value-leaf (value ON, K5/a8) at depth 5
Reports V5 vs H5 (MATCHED depth), V5 vs H4 (+1 depth advantage), V5 vs H3 (+2). Hypothesis: value-leaf
LOSES at matched depth (leaf residual) but WINS at +1/+2 (extra depth outweighs it). If V5's LP lands
between H4 and H5, the value leaf is worth ~"heuristic depth minus a fraction". LP = loss-penalised avg win
turn (loss=mt+1); lower = better. See docs/design/learned-d0-policy.md.

    scripts/valueleaf_depth_mismatch.py --games 500 --seeds 4004 5005 6006 --decks TH burn antilife
"""
import argparse, os, re, subprocess, time

MTG = "build/Release/mtg"
DECKS = {
    "antilife": ("decks/Anti-Lifegain.cod", "decks/Anti-Lifegain.value.json", 8),
    "slivers":  ("decks/slivers_vial.txt",  "decks/slivers_vial.value.json",  8),
    "TH":       ("decks/treasure_hunt.txt", "decks/treasure_hunt.value.json", 8),
    "burn":     ("decks/burn.txt",          "decks/burn.value.json",          8),
    "knights":  ("decks/Knights.cod",       "decks/Knights.value.json",       8),
}


def run(deck, depth, games, seed, mt, threads, profile, value_on):
    env = dict(os.environ)
    for k in ("MTG_EVAL_MODEL","MTG_EVAL_PROFILE","MTG_VALUE_MODEL","MTG_VALUE_PROFILE",
              "MTG_NC_SEARCH","MTG_VALUE_MIN_DEPTH","MTG_VALUE_REDO_MODE","MTG_VALUE_STARTGATE_ALPHA"):
        env.pop(k, None)
    if value_on:
        env["MTG_VALUE_MODEL"] = "1"; env["MTG_VALUE_PROFILE"] = profile
        env["MTG_VALUE_MIN_DEPTH"] = "5"; env["MTG_VALUE_STARTGATE_ALPHA"] = "8"
    else:
        env["MTG_VALUE_MODEL"] = "0"
    cmd = [MTG, deck, "--games", str(games), "--seed", str(seed),
           "--depth", str(depth), "--max-turns", str(mt), "--threads", str(threads)]
    t0 = time.time()
    out = subprocess.run(cmd, capture_output=True, text=True, env=env).stdout
    dt = time.time() - t0
    p = int(re.search(r"Games played\s*:\s*(\d+)", out).group(1))
    w = int(re.search(r"Games won\s*:\s*(\d+)", out).group(1))
    m = re.search(r"Avg win turn\s*:\s*([\d.]+)", out)
    a = float(m.group(1)) if m else float("nan")
    return (w*a + (p-w)*(mt+1)) / p, 1000.0*dt/p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--games", type=int, default=500)
    ap.add_argument("--seeds", nargs="+", type=int, default=[4004, 5005, 6006])
    ap.add_argument("--threads", type=int, default=6)
    ap.add_argument("--decks", nargs="+", default=["TH", "burn", "antilife"])
    ap.add_argument("--out", default="logs/eval/valueleaf_depth_mismatch.txt")
    args = ap.parse_args()
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    of = open(args.out, "a")
    def emit(s):
        print(s, flush=True); of.write(s+"\n"); of.flush()
    emit("\n===== DEPTH-MISMATCH (UNBOUNDED)  V5=value-leaf-d5 vs Hn=heuristic-dn  games=%d =====" % args.games)
    emit("LP (loss=mt+1, lower=better) then [ms/game].  dLP = V5 - Hn (negative = value-leaf better)")
    emit("%-8s %5s   %-15s %-15s %-15s   %-15s   %8s %8s %8s" % (
        "deck","seed","H3 LP[ms]","H4 LP[ms]","H5 LP[ms]","V5 LP[ms]","V5-H5","V5-H4","V5-H3"))
    agg = {}
    for dname in args.decks:
        deck, prof, mt = DECKS[dname]
        for seed in args.seeds:
            try:
                h3,h3t = run(deck,3,args.games,seed,mt,args.threads,None,False)
                h4,h4t = run(deck,4,args.games,seed,mt,args.threads,None,False)
                h5,h5t = run(deck,5,args.games,seed,mt,args.threads,None,False)
                v5,v5t = run(deck,5,args.games,seed,mt,args.threads,prof,True)
                a=agg.setdefault(dname,[0,0,0,0]); a[0]+=v5-h5; a[1]+=v5-h4; a[2]+=v5-h3; a[3]+=1
                emit("%-8s %5d   %7.4f[%5.1f] %7.4f[%5.1f] %7.4f[%5.1f]   %7.4f[%5.1f]   %+8.4f %+8.4f %+8.4f" % (
                    dname,seed,h3,h3t,h4,h4t,h5,h5t,v5,v5t, v5-h5, v5-h4, v5-h3))
            except Exception as e:
                emit("%-8s %5d  ERROR %s" % (dname,seed,e))
    emit("--- mean dLP over seeds (negative = value-leaf BETTER) ---")
    for dname,a in agg.items():
        n=a[3] or 1
        emit("  %-8s  V5-H5=%+.4f  V5-H4=%+.4f  V5-H3=%+.4f" % (dname, a[0]/n, a[1]/n, a[2]/n))
    of.close()


if __name__ == "__main__":
    main()

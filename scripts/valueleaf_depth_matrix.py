#!/usr/bin/env python3
"""Full value-leaf x heuristic DEPTH MATRIX (UNBOUNDED) with cost, to (1) separate decks where the value
leaf is ~EXACT (no heuristic needed) from those that GENUINELY need the heuristic, and (2) calibrate the
per-model "trust depth": at what value-leaf depth does it match heuristic-at-D?

CRITICAL (2026-07-11 fix): measures the PURE value-leaf by DISABLING the hybrid redo (--value-min-depth 0,
env MTG_VALUE_MIN_DEPTH=0). The earlier run used MIN_DEPTH=5, which made every committed depth < 5 RE-RUN
the heuristic -- so V3/V4 were the HYBRID (heuristic on the leaf-dependent games) and only V5 was the raw
leaf. That confound produced the spurious "value-leaf exact at d3/d4, worse only at d5" + the impossible
"V4 beats V5" (a deeper search cannot be worse unless d3/d4 were secretly the heuristic). With MIN_DEPTH=0
every V_d is the raw value-leaf at depth d, so the true crossover (where the heuristic wins) is visible.

All UNBOUNDED (budget 0) so each config reaches its nominal depth. Per deck/seed runs heuristic (value OFF)
at each --hdepths and value-leaf (value ON, MIN_DEPTH=<flag>/a8) at each --vdepths; records LP (loss=mt+1,
lower=better) and wall ms/game. Prints per-deck means + the Vi-Hj difference matrix (negative = value-leaf
better). The binary default is value-ON, so the OFF arm sets MTG_VALUE_MODEL=0 EXPLICITLY. Include low
--vdepths (1,2) to see the leaf-dominated regime where the heuristic's full rollout should win.
See learned-d0-policy.md.

    scripts/valueleaf_depth_matrix.py --games 1000 --seeds 8008 9009 10010 11011 \
        --hdepths 1 2 3 4 5 --vdepths 1 2 3 4 5 --value-min-depth 0
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


def run(deck, depth, games, seed, mt, threads, profile, value_on, value_min_depth):
    env = dict(os.environ)
    for k in ("MTG_EVAL_MODEL","MTG_EVAL_PROFILE","MTG_VALUE_MODEL","MTG_VALUE_PROFILE",
              "MTG_NC_SEARCH","MTG_VALUE_MIN_DEPTH","MTG_VALUE_REDO_MODE","MTG_VALUE_STARTGATE_ALPHA"):
        env.pop(k, None)
    if value_on:
        env["MTG_VALUE_MODEL"]="1"; env["MTG_VALUE_PROFILE"]=profile
        # MIN_DEPTH=0 => PURE value-leaf (no heuristic redo); the whole point of this matrix.
        env["MTG_VALUE_MIN_DEPTH"]=str(value_min_depth); env["MTG_VALUE_STARTGATE_ALPHA"]="8"
    else:
        env["MTG_VALUE_MODEL"]="0"
    cmd=[MTG,deck,"--games",str(games),"--seed",str(seed),"--depth",str(depth),
         "--max-turns",str(mt),"--threads",str(threads)]
    t0=time.time(); out=subprocess.run(cmd,capture_output=True,text=True,env=env).stdout; dt=time.time()-t0
    p=int(re.search(r"Games played\s*:\s*(\d+)",out).group(1))
    w=int(re.search(r"Games won\s*:\s*(\d+)",out).group(1))
    m=re.search(r"Avg win turn\s*:\s*([\d.]+)",out); a=float(m.group(1)) if m else float("nan")
    return (w*a+(p-w)*(mt+1))/p, 1000.0*dt/p


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--games",type=int,default=1000)
    ap.add_argument("--seeds",nargs="+",type=int,default=[4004,5005,6006,7007])
    ap.add_argument("--hdepths",nargs="+",type=int,default=[1,2,3,4,5])
    ap.add_argument("--vdepths",nargs="+",type=int,default=[1,2,3,4,5])
    ap.add_argument("--value-min-depth",type=int,default=0,
                    help="MTG_VALUE_MIN_DEPTH for the value arm; 0 = PURE value-leaf (no redo). Default 0.")
    ap.add_argument("--threads",type=int,default=6)
    ap.add_argument("--decks",nargs="+",default=list(DECKS))
    ap.add_argument("--out",default="logs/eval/valueleaf_depth_matrix.txt")
    args=ap.parse_args()
    os.makedirs(os.path.dirname(args.out),exist_ok=True)
    of=open(args.out,"a")
    def emit(s): print(s,flush=True); of.write(s+"\n"); of.flush()
    emit("\n===== DEPTH MATRIX (UNBOUNDED)  games=%d seeds=%s value_min_depth=%d%s =====" % (
        args.games, args.seeds, args.value_min_depth,
        "  [PURE value-leaf, no redo]" if args.value_min_depth == 0 else "  [HYBRID redo below this]"))
    for dname in args.decks:
        deck,prof,mt=DECKS[dname]
        # accumulate mean LP + ms per config over seeds
        H={d:[0.0,0.0] for d in args.hdepths}; V={d:[0.0,0.0] for d in args.vdepths}; n=0
        for seed in args.seeds:
            try:
                for d in args.hdepths:
                    lp,ms=run(deck,d,args.games,seed,mt,args.threads,None,False,args.value_min_depth); H[d][0]+=lp; H[d][1]+=ms
                for d in args.vdepths:
                    lp,ms=run(deck,d,args.games,seed,mt,args.threads,prof,True,args.value_min_depth); V[d][0]+=lp; V[d][1]+=ms
                n+=1
            except Exception as e:
                emit("  %s s%d ERROR %s" % (dname,seed,e))
        if not n: continue
        emit("---- %s (mean over %d seeds) ----" % (dname,n))
        emit("  heuristic:  " + "   ".join("H%d=%.4f[%.1fms]"%(d,H[d][0]/n,H[d][1]/n) for d in args.hdepths))
        emit("  value-leaf: " + "   ".join("V%d=%.4f[%.1fms]"%(d,V[d][0]/n,V[d][1]/n) for d in args.vdepths))
        emit("  Vi-Hj matrix (neg = value-leaf better):")
        emit("        " + "  ".join("H%d    "%d for d in args.hdepths))
        for vi in args.vdepths:
            row="   V%d  " % vi
            for hj in args.hdepths:
                row += "%+.4f " % (V[vi][0]/n - H[hj][0]/n)
            emit(row)
    of.close()


if __name__ == "__main__":
    main()

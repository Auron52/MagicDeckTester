#!/usr/bin/env python3
"""Calibrate + write the value-leaf's `value_trust_depth` INTO its profile (<deck>.value.json).

The raw value leaf is a weak-but-cheap evaluator: measured, it only reaches converged-heuristic quality at
some deck-specific depth, and below that an UNVERIFIED committed line plays materially worse than the
heuristic (see docs/design/learned-d0-policy.md). `value_trust_depth` tells the engine the shallowest depth
at which the raw leaf is trustworthy: at/above it the hybrid KEEPS the value-leaf line; below it (and not a
verified win) it escalates to one heuristic search on the remaining budget.

This script DERIVES that number instead of hand-setting it -- it is the "decide its usage" testing the user
asked to fold into model creation. Per deck it runs, UNBOUNDED (no pruning confound):
  heuristic  H_d  (MTG_VALUE_MODEL=0)         for d in --depths
  pure leaf  V_d  (MTG_VALUE_MODEL=1, MIN_DEPTH=0)  for d in --depths
computes the converged heuristic quality H_conv = min_d H_d, and sets

  value_trust_depth = min { d : V_d - H_conv <= --tol }     (or UNSET if the leaf never converges within tol,
                                                              meaning "escalate at every depth up to user depth")

then writes it into the profile (unless --dry-run). Intended to run once per value model, right after the
model is created (e.g. from analyze-deck / the training pipeline). LP = loss-penalised avg win turn
(loss=max_turns+1, lower=better).

    scripts/valueleaf_calibrate_trust.py --games 500 --seeds 8008 9009 --depths 2 3 4 5 6 --tol 0.002
    scripts/valueleaf_calibrate_trust.py --decks knights slivers --dry-run
"""
import argparse, collections, json, os, re, subprocess

MTG = "build/Release/mtg"
# Per-deck folder layout (decks/<name>/<name>...). Earlier flat paths (decks/<name>.value.json) are stale.
DECKS = {
    "antilife": ("decks/Anti-Lifegain/Anti-Lifegain.cod", "decks/Anti-Lifegain/Anti-Lifegain.value.json", 8),
    "slivers":  ("decks/slivers_vial/slivers_vial.txt",   "decks/slivers_vial/slivers_vial.value.json",   8),
    "TH":       ("decks/treasure_hunt/treasure_hunt.txt", "decks/treasure_hunt/treasure_hunt.value.json", 8),
    "burn":     ("decks/burn/burn.txt",                   "decks/burn/burn.value.json",                   8),
    "knights":  ("decks/Knights/Knights.cod",             "decks/Knights/Knights.value.json",             8),
    "hinata":   ("decks/Hinata2/Hinata2.cod",             "decks/Hinata2/Hinata2.value.json",             8),
}


def lp(deck, depth, games, seed, mt, threads, profile, value_on):
    env = dict(os.environ)
    for k in ("MTG_EVAL_MODEL","MTG_EVAL_PROFILE","MTG_VALUE_MODEL","MTG_VALUE_PROFILE",
              "MTG_NC_SEARCH","MTG_VALUE_MIN_DEPTH","MTG_VALUE_REDO_MODE","MTG_VALUE_STARTGATE_ALPHA"):
        env.pop(k, None)
    if value_on:
        env["MTG_VALUE_MODEL"]="1"; env["MTG_VALUE_PROFILE"]=profile
        env["MTG_VALUE_MIN_DEPTH"]="0"; env["MTG_VALUE_STARTGATE_ALPHA"]="8"   # 0 => PURE leaf, no escalation
    else:
        env["MTG_VALUE_MODEL"]="0"
    cmd=[MTG,deck,"--games",str(games),"--seed",str(seed),"--depth",str(depth),
         "--max-turns",str(mt),"--threads",str(threads)]
    out=subprocess.run(cmd,capture_output=True,text=True,env=env).stdout
    p=int(re.search(r"Games played\s*:\s*(\d+)",out).group(1))
    w=int(re.search(r"Games won\s*:\s*(\d+)",out).group(1))
    m=re.search(r"Avg win turn\s*:\s*([\d.]+)",out); a=float(m.group(1)) if m else float("nan")
    return (w*a+(p-w)*(mt+1))/p


def write_trust(profile_path, depth):
    d=json.load(open(profile_path), object_pairs_hook=collections.OrderedDict)
    nd=collections.OrderedDict()
    if depth is not None: nd["value_trust_depth"]=depth
    for k,v in d.items():
        if k!="value_trust_depth": nd[k]=v
    # Match the trained sidecar's format (default ", "/": " separators, single line) so a trust-depth
    # change is a minimal diff rather than a whole-file reformat.
    json.dump(nd, open(profile_path,"w"))


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--games",type=int,default=500)
    ap.add_argument("--seeds",nargs="+",type=int,default=[8008,9009])
    ap.add_argument("--depths",nargs="+",type=int,default=[2,3,4,5,6])
    ap.add_argument("--tol",type=float,default=0.002,help="max LP gap V_d - H_conv to call the leaf trusted")
    ap.add_argument("--threads",type=int,default=0)
    ap.add_argument("--decks",nargs="+",default=list(DECKS))
    ap.add_argument("--dry-run",action="store_true",help="print the decision but do NOT write the profile")
    ap.add_argument("--out",default="logs/eval/valueleaf_calibrate_trust.txt")
    args=ap.parse_args()
    os.makedirs(os.path.dirname(args.out),exist_ok=True)
    of=open(args.out,"a")
    def emit(s): print(s,flush=True); of.write(s+"\n"); of.flush()
    emit("\n===== CALIBRATE value_trust_depth  games=%d seeds=%s depths=%s tol=%.4f%s =====" % (
        args.games,args.seeds,args.depths,args.tol,"  [DRY-RUN]" if args.dry_run else ""))
    for dname in args.decks:
        deck,prof,mt=DECKS[dname]
        H={d:0.0 for d in args.depths}; V={d:0.0 for d in args.depths}; n=0
        for seed in args.seeds:
            for d in args.depths:
                H[d]+=lp(deck,d,args.games,seed,mt,args.threads,None,False)
                V[d]+=lp(deck,d,args.games,seed,mt,args.threads,prof,True)
            n+=1
        for d in args.depths: H[d]/=n; V[d]/=n
        hconv=min(H.values())
        trusted=[d for d in sorted(args.depths) if V[d]-hconv <= args.tol]
        trust=trusted[0] if trusted else None
        emit("---- %s  (H_conv=%.4f) ----" % (dname,hconv))
        emit("   " + "  ".join("d%d:H=%.4f/V=%.4f(%+.4f)"%(d,H[d],V[d],V[d]-hconv) for d in sorted(args.depths)))
        emit("   => value_trust_depth = %s  %s" % (
            trust if trust is not None else "UNSET (leaf never within tol -> escalate at every depth)",
            "" if args.dry_run else "(written to %s)"%prof))
        if not args.dry_run:
            write_trust(prof, trust)
    of.close()


if __name__=="__main__":
    main()

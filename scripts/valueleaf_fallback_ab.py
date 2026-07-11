#!/usr/bin/env python3
"""A/B the DEPTH-AWARE FALLBACK (2026-07-11) against the pure heuristic, by the PRIMARY metric LP
(loss-penalised avg win turn, loss = max_turns+1). Three arms per deck/seed/budget:

  off   : pure heuristic            MTG_VALUE_MODEL=0
  pure  : value-leaf, NO fallback   MTG_VALUE_MODEL=1  MTG_VALUE_MIN_DEPTH=0   (raw weak leaf)
  fb    : value-leaf + fallback     MTG_VALUE_MODEL=1  (NO MIN_DEPTH -> new profile-driven default:
                                     knights/slivers keep@d5 via value_trust_depth, others escalate)

Reports LP and ms/game for each arm, and dLP = fb - off (target ~<=0: fallback recovers the pure leaf's
generous-budget residual) and pure - off (the residual it is recovering). Run UNBOUNDED (budget 0) to see
the generous-budget regime AND at the node gate (d3=10/d5=20) to confirm the gate stays neutral.
See docs/design/learned-d0-policy.md.

    scripts/valueleaf_fallback_ab.py --games 500 --seeds 8008 9009 10010 11011 --depth 5 --budget 0
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


def run(deck, depth, games, seed, mt, threads, profile, budget, arm):
    env = dict(os.environ)
    for k in ("MTG_EVAL_MODEL","MTG_EVAL_PROFILE","MTG_VALUE_MODEL","MTG_VALUE_PROFILE",
              "MTG_NC_SEARCH","MTG_VALUE_MIN_DEPTH","MTG_VALUE_REDO_MODE","MTG_VALUE_STARTGATE_ALPHA"):
        env.pop(k, None)
    if arm == "off":
        env["MTG_VALUE_MODEL"] = "0"
    else:
        env["MTG_VALUE_MODEL"] = "1"; env["MTG_VALUE_PROFILE"] = profile
        env["MTG_VALUE_STARTGATE_ALPHA"] = "8"
        if arm == "pure":
            env["MTG_VALUE_MIN_DEPTH"] = "0"        # no escalation -> raw value leaf
        # arm == "fb": leave MIN_DEPTH unset -> new profile-driven default (fallback active)
    cmd = [MTG, deck, "--games", str(games), "--seed", str(seed),
           "--depth", str(depth), "--max-turns", str(mt), "--threads", str(threads)]
    if budget and budget > 0:
        cmd += ["--budget-ms", str(budget)]
    t0 = time.time()
    out = subprocess.run(cmd, capture_output=True, text=True, env=env).stdout
    dt = time.time() - t0
    p = int(re.search(r"Games played\s*:\s*(\d+)", out).group(1))
    w = int(re.search(r"Games won\s*:\s*(\d+)", out).group(1))
    m = re.search(r"Avg win turn\s*:\s*([\d.]+)", out)
    a = float(m.group(1)) if m else float("nan")
    return (w*a + (p-w)*(mt+1)) / p, 1000.0*dt/p, w, p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--games", type=int, default=500)
    ap.add_argument("--seeds", nargs="+", type=int, default=[8008, 9009, 10010, 11011])
    ap.add_argument("--depth", type=int, default=5)
    ap.add_argument("--budget", type=int, default=0, help="budget-ms (0 = unbounded)")
    ap.add_argument("--threads", type=int, default=0)
    ap.add_argument("--decks", nargs="+", default=list(DECKS))
    ap.add_argument("--out", default="logs/eval/valueleaf_fallback_ab.txt")
    args = ap.parse_args()
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    of = open(args.out, "a")
    def emit(s): print(s, flush=True); of.write(s+"\n"); of.flush()
    emit("\n===== FALLBACK A/B  d%d  budget=%s  games=%d seeds=%s =====" % (
        args.depth, args.budget if args.budget else "UNBOUNDED", args.games, args.seeds))
    emit("%-9s %10s %10s %10s   %9s %9s   %s" % (
        "deck","off LP","pure LP","fb LP","fb-off","pure-off","[off/pure/fb ms]"))
    agg = {}
    for dname in args.decks:
        deck, prof, mt = DECKS[dname]
        lo=lp=lf=0.0; mo=mp=mf=0.0; n=0
        for seed in args.seeds:
            try:
                o,om,_,_  = run(deck,args.depth,args.games,seed,mt,args.threads,prof,args.budget,"off")
                pu,pm,_,_ = run(deck,args.depth,args.games,seed,mt,args.threads,prof,args.budget,"pure")
                fb,fm,_,_ = run(deck,args.depth,args.games,seed,mt,args.threads,prof,args.budget,"fb")
                lo+=o; lp+=pu; lf+=fb; mo+=om; mp+=pm; mf+=fm; n+=1
            except Exception as e:
                emit("  %s s%d ERROR %s" % (dname,seed,e))
        if not n: continue
        lo/=n; lp/=n; lf/=n; mo/=n; mp/=n; mf/=n
        agg[dname]=(lf-lo, lp-lo)
        emit("%-9s %10.4f %10.4f %10.4f   %+9.4f %+9.4f   [%.1f/%.1f/%.1f]" % (
            dname, lo, lp, lf, lf-lo, lp-lo, mo, mp, mf))
    emit("--- summary (dLP vs pure heuristic; fb should be <= pure) ---")
    for dname,(fbo,puo) in agg.items():
        tag = "  fallback RECOVERS" if fbo < puo - 1e-9 else ("  no-change" if abs(fbo-puo)<1e-9 else "  fb WORSE than pure?!")
        emit("  %-9s fb-off=%+.4f  pure-off=%+.4f%s" % (dname, fbo, puo, tag))
    of.close()


if __name__ == "__main__":
    main()

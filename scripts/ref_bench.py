#!/usr/bin/env python3
"""Per-game reference benchmark: HUMAN vs CLAIRVOYANT search vs NON-CLAIRVOYANT search on
IDENTICAL opening hands (the references' exact mulligan is forced into every policy).

For each references/<deck>/*.json: reconstruct the ref's exact opening hand via --force-mulligan,
run the clairvoyant search (--depth D, no NC env) and the NC reshuffle search (MTG_NC_SEARCH K/d),
and print per-game win turns beside the human's saved win turn. Aggregates loss-penalised LP
(losses = max_turns+1). This is the ONLY correct way to compare the search to references
(the runner otherwise mulligans autonomously -> different hands). See learned-d0-policy.md.

    scripts/ref_bench.py --deck antilife --nc-k 8 --nc-depth 2
"""
import argparse, json, os, re, subprocess, sys, glob
from concurrent.futures import ThreadPoolExecutor

MTG = "build/Release/mtg"
DECKS = {
    "antilife": ("references/Anti-Lifegain", "decks/Anti-Lifegain.cod"),
    "burn":     ("references/burn",           "decks/burn.txt"),
    "slivers":  ("references/slivers_vial",   "decks/slivers_vial.txt"),
    "knights":  ("references/Knights",        "decks/Knights.cod"),
    "TH":       ("references/treasure_hunt",  "decks/treasure_hunt.txt"),
    "hinata":   ("references/Hinata2",        "decks/Hinata2.cod"),
}


def run_one(deck_file, seed, gi, mull, depth, max_turns, nc):
    env = dict(os.environ)
    for k in ("MTG_EVAL_MODEL","MTG_VALUE_MODEL","MTG_NC_SEARCH","MTG_NC_K","MTG_NC_DEPTH"):
        env.pop(k, None)
    if nc:
        env["MTG_NC_SEARCH"] = "1"; env["MTG_NC_K"] = str(nc[0]); env["MTG_NC_DEPTH"] = str(nc[1])
    fm = "%d:%s" % (mull["count"], ",".join(str(x) for x in mull.get("bottom", [])))
    cmd = [MTG, deck_file, "--games", "1", "--seed", str(seed), "--game-index", str(gi),
           "--depth", str(depth), "--max-turns", str(max_turns), "--force-mulligan", fm, "--threads", "1"]
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, env=env, timeout=180)
    except subprocess.TimeoutExpired:
        sys.stderr.write("TIMEOUT seed=%d gi=%d nc=%s\n" % (seed, gi, nc)); return "TMO"
    m = re.search(r"Games won\s*:\s*(\d+)", p.stdout)
    if m is None:
        sys.stderr.write("PARSE-FAIL rc=%d seed=%d gi=%d nc=%s\n  cmd=%s\n  stderr=%s\n" % (
            p.returncode, seed, gi, nc, " ".join(cmd), p.stderr.strip()[-400:]))
        return "ERR"
    if int(m.group(1)):
        return int(float(re.search(r"Avg win turn\s*:\s*([\d.]+)", p.stdout).group(1)))
    return None  # loss


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--deck", required=True, choices=list(DECKS))
    ap.add_argument("--depth", type=int, default=5)
    ap.add_argument("--max-turns", type=int, default=8)
    ap.add_argument("--nc-k", type=int, default=8)
    ap.add_argument("--nc-depth", type=int, default=2)
    ap.add_argument("--threads", type=int, default=12)
    args = ap.parse_args()
    refdir, deck_file = DECKS[args.deck]
    refs = sorted(glob.glob(os.path.join(refdir, "*.json")))
    mt = args.max_turns
    LOSS = mt + 1

    def work(path):
        r = json.load(open(path))
        seed, gi = r["seed"], r["game_index"]
        mull = r.get("mulligan", {"count": 0, "bottom": []})
        human = r["win_turn"] if r.get("won") else None
        clair = run_one(deck_file, seed, gi, mull, args.depth, mt, None)
        ncwt  = run_one(deck_file, seed, gi, mull, args.depth, mt, (args.nc_k, args.nc_depth))
        return (os.path.basename(path), seed, gi, human, clair, ncwt)

    def lp(v): return v if isinstance(v, int) else LOSS  # None(loss) or "ERR"/"TMO" -> LOSS
    def cell(v): return str(v) if isinstance(v, int) else (v if v in ("ERR","TMO") else "LOSS")
    print("%-26s %5s %4s | %6s %6s %6s | %s" % ("ref","seed","gi","human","clair","NC","flags"))
    sys.stdout.flush()
    sums = {"human":0,"clair":0,"nc":0}; n=0
    with ThreadPoolExecutor(max_workers=args.threads) as ex:
        # stream each row as it completes so partial progress survives an OOM/timeout
        for name, seed, gi, h, c, nc in ex.map(work, refs):
            n += 1
            sums["human"]+=lp(h); sums["clair"]+=lp(c); sums["nc"]+=lp(nc)
            flags=[]
            hi, ci, nci = (x if isinstance(x,int) else None for x in (h,c,nc))
            # search worse than human (search-quality gaps -- the optimization target)
            if nci is not None and hi is not None and nci > hi: flags.append("NC>human+%d"%(nci-hi))
            if nci is not None and ci is not None and nci > ci: flags.append("NC>clair+%d"%(nci-ci))
            # human worse than a policy (human fell short -- the user's side question)
            if hi is not None and nci is not None and hi > nci: flags.append("human>NC+%d"%(hi-nci))
            if hi is not None and ci is not None and hi > ci and not (nci is not None and hi>nci):
                flags.append("human>clair+%d(EVPI?)"%(hi-ci))
            if nc is None: flags.append("NC-LOSS")
            if c is None: flags.append("CLAIR-LOSS")
            print("%-26s %5s %4s | %6s %6s %6s | %s" % (
                name, seed, gi, cell(h), cell(c), cell(nc), " ".join(flags)))
            sys.stdout.flush()
    print("-"*80)
    print("%-26s %5s %4s | %6.3f %6.3f %6.3f | LP (losses=%d), n=%d" % (
        "LP AVG","","", sums["human"]/n, sums["clair"]/n, sums["nc"]/n, LOSS, n))


if __name__ == "__main__":
    main()

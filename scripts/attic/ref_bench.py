#!/usr/bin/env python3
"""Per-game reference benchmark: HUMAN vs CLAIRVOYANT search vs NON-CLAIRVOYANT search on
IDENTICAL opening hands (the references' exact mulligan is forced into every policy).

For each references/<deck>/*.json: reconstruct the ref's exact opening hand via --force-mulligan,
run the shipped clairvoyant policy (the deck's value_play; no NC env) and the NC reshuffle search
(MTG_NC_SEARCH K/d), and print per-game win turns beside the human's saved win turn. Aggregates
loss-penalised LP (losses = max_turns+1). This is the ONLY correct way to compare the search to
references (the runner otherwise mulligans autonomously -> different hands). See learned-d0-policy.md.

    scripts/ref_bench.py --deck antilife --nc-k 8 --nc-depth 2
    scripts/ref_bench.py --deck all --out logs/ref_bench

DEPTH: by default --depth is OMITTED so the engine uses the deck's committed value_play policy
(the thing that actually ships). Every reference deck now has an *enabled* value_play lock, so
passing an explicit --depth requires --ignore-play-profile -- this script adds it automatically.

MEMORY: the NC search uses ~3 GB RSS/game (uncapped reshuffle memo). --nc-threads is deliberately
much smaller than --threads; raising it risks the OOM killer SIGKILLing siblings (rc=-9).
"""
import argparse, json, os, re, subprocess, sys, glob
from concurrent.futures import ThreadPoolExecutor

MTG = "build/Release/mtg"
# Per-deck folder layout (docs/design/per-deck-folder-layout.md).
DECKS = {
    "antilife":    ("references/Anti-Lifegain", "decks/Anti-Lifegain/Anti-Lifegain.cod"),
    "burn":        ("references/burn",          "decks/burn/burn.txt"),
    "slivers":     ("references/slivers_vial",  "decks/slivers_vial/slivers_vial.txt"),
    "knights":     ("references/Knights",       "decks/Knights/Knights.cod"),
    "TH":          ("references/treasure_hunt", "decks/treasure_hunt/treasure_hunt.txt"),
    "hinata":      ("references/Hinata2",       "decks/Hinata2/Hinata2.cod"),
    "auras":       ("references/Auras",         "decks/Auras/Auras.cod"),
    "dragonstorm": ("references/Dragonstorm",   "decks/Dragonstorm/Dragonstorm.cod"),
}


def run_one(deck_file, seed, gi, mull, depth, max_turns, nc, log_dir=None, tag=""):
    env = dict(os.environ)
    for k in ("MTG_EVAL_MODEL","MTG_VALUE_MODEL","MTG_NC_SEARCH","MTG_NC_K","MTG_NC_DEPTH"):
        env.pop(k, None)
    if nc:
        env["MTG_NC_SEARCH"] = "1"; env["MTG_NC_K"] = str(nc[0]); env["MTG_NC_DEPTH"] = str(nc[1])
    mull = mull or {"count": 0, "bottom": []}
    fm = "%d:%s" % (mull.get("count", 0), ",".join(str(x) for x in (mull.get("bottom") or [])))
    cmd = [MTG, deck_file, "--games", "1", "--seed", str(seed), "--game-index", str(gi),
           "--max-turns", str(max_turns), "--force-mulligan", fm, "--threads", "1"]
    if depth is not None:
        # Every reference deck has an enabled value_play lock; an explicit depth must bypass it.
        cmd += ["--depth", str(depth), "--ignore-play-profile"]
    if log_dir:
        d = os.path.join(log_dir, tag)
        os.makedirs(d, exist_ok=True)
        cmd += ["--log-dir", d]
    p = subprocess.run(cmd, capture_output=True, text=True, env=env)
    # Single-game summary: "avg (turns) : T" where T is the win turn, or max_turns+1 on a loss
    # (the loss-penalised avg collapses to those two cases for --games 1).
    m = re.search(r"avg \(turns\)\s*:\s*([\d.]+)", p.stdout)
    if m is None:
        sys.stderr.write("PARSE-FAIL rc=%d seed=%d gi=%d nc=%s\n  cmd=%s\n  stderr=%s\n" % (
            p.returncode, seed, gi, nc, " ".join(cmd), p.stderr.strip()[-400:]))
        return "ERR"
    v = float(m.group(1))
    if v >= max_turns + 1 - 1e-9:
        return None  # unwon within max_turns
    return int(round(v))


def bench_deck(deck, args):
    refdir, deck_file = DECKS[deck]
    refs = sorted(glob.glob(os.path.join(refdir, "*.json")))
    if not refs:
        return None
    games = []
    for path in refs:
        r = json.load(open(path))
        games.append({
            "name": os.path.basename(path), "seed": r["seed"], "gi": r["game_index"],
            "mull": r.get("mulligan") or {"count": 0, "bottom": []},
            "human": r["win_turn"] if r.get("won") else None,
        })
    # max_turns must cover the slowest HUMAN win, else a real human win reads as a loss.
    hw = [g["human"] for g in games if g["human"]]
    mt = args.max_turns if args.max_turns else max(8, max(hw) if hw else 8)
    LOSS = mt + 1
    for g in games:                       # a human win beyond the horizon is a loss on this scale
        if g["human"] is not None and g["human"] > mt: g["human"] = None

    def clair(g):
        return run_one(deck_file, g["seed"], g["gi"], g["mull"], args.depth, mt, None,
                       args.out and os.path.join(args.out, deck, "clair"), "s%d_gi%d" % (g["seed"], g["gi"]))

    def ncrun(g):
        return run_one(deck_file, g["seed"], g["gi"], g["mull"], args.depth, mt, (args.nc_k, args.nc_depth),
                       args.out and os.path.join(args.out, deck, "nc"), "s%d_gi%d" % (g["seed"], g["gi"]))

    sys.stderr.write("[%s] n=%d max_turns=%d  clairvoyant pass...\n" % (deck, len(games), mt))
    with ThreadPoolExecutor(max_workers=args.threads) as ex:
        for g, v in zip(games, ex.map(clair, games)): g["clair"] = v
    if not args.no_nc:
        sys.stderr.write("[%s] NC pass (K=%d d=%d, %d-way)...\n" % (deck, args.nc_k, args.nc_depth, args.nc_threads))
        with ThreadPoolExecutor(max_workers=args.nc_threads) as ex:
            for g, v in zip(games, ex.map(ncrun, games)): g["nc"] = v
    else:
        for g in games: g["nc"] = "SKIP"

    def lp(v): return v if isinstance(v, int) else LOSS  # None(loss)/"ERR" -> LOSS
    def cell(v): return str(v) if isinstance(v, int) else (v if isinstance(v, str) else "LOSS")
    print("\n=== %s  (n=%d, max_turns=%d, losses=%d)" % (deck, len(games), mt, LOSS))
    print("%-26s %5s %4s | %6s %6s %6s | %s" % ("ref","seed","gi","human","clair","NC","flags"))
    sums = {"human":0,"clair":0,"nc":0}
    for g in games:
        h, c, nc = g["human"], g["clair"], g["nc"]
        sums["human"]+=lp(h); sums["clair"]+=lp(c)
        if not args.no_nc: sums["nc"]+=lp(nc)
        hi, ci, nci = (x if isinstance(x,int) else None for x in (h,c,nc))
        flags=[]
        # ---- search falls short of the human (the optimization target) ----
        if ci is not None and hi is not None and ci > hi: flags.append("CLAIR>human+%d"%(ci-hi))
        if c is None and hi is not None:                  flags.append("CLAIR-LOSS(human %d)"%hi)
        if nci is not None and hi is not None and nci > hi: flags.append("NC>human+%d"%(nci-hi))
        if nc is None and hi is not None:                 flags.append("NC-LOSS(human %d)"%hi)
        if nci is not None and ci is not None and nci > ci: flags.append("NC>clair+%d"%(nci-ci))
        # ---- human falls short of a policy (the user's side question) ----
        if hi is not None and ci is not None and hi > ci: flags.append("human>clair+%d"%(hi-ci))
        if h is None and ci is not None:                  flags.append("human-LOSS(clair %d)"%ci)
        g["flags"] = flags
        print("%-26s %5s %4s | %6s %6s %6s | %s" % (
            g["name"], g["seed"], g["gi"], cell(h), cell(c), cell(nc), " ".join(flags)))
        sys.stdout.flush()
    n = len(games)
    print("%-26s %5s %4s | %6.3f %6.3f %6s | LP (losses=%d), n=%d" % (
        "LP AVG","","", sums["human"]/n, sums["clair"]/n,
        "  -   " if args.no_nc else "%6.3f" % (sums["nc"]/n), LOSS, n))
    return {"deck": deck, "n": n, "max_turns": mt, "games": games,
            "lp": {k: sums[k]/n for k in sums}}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--deck", required=True, choices=list(DECKS) + ["all"])
    ap.add_argument("--depth", type=int, default=None,
                    help="explicit lookahead depth (adds --ignore-play-profile); "
                         "default = omit, i.e. use the deck's committed value_play policy")
    ap.add_argument("--max-turns", type=int, default=0, help="0 = auto (max(8, slowest human win))")
    ap.add_argument("--nc-k", type=int, default=8)
    ap.add_argument("--nc-depth", type=int, default=2)
    ap.add_argument("--threads", type=int, default=12, help="concurrency for the clairvoyant pass")
    ap.add_argument("--nc-threads", type=int, default=6,
                    help="concurrency for the NC pass (~3 GB RSS/game -- keep small)")
    ap.add_argument("--no-nc", action="store_true", help="clairvoyant pass only")
    ap.add_argument("--out", default=None, help="directory for per-game JSON logs + summary.json")
    args = ap.parse_args()

    decks = list(DECKS) if args.deck == "all" else [args.deck]
    results = [r for r in (bench_deck(d, args) for d in decks) if r]

    if len(results) > 1:
        print("\n=== SUMMARY (per-deck LP; lower is better)")
        print("%-14s %4s %7s %7s %7s   %s" % ("deck","n","human","clair","NC","shortfalls"))
        for r in results:
            short = sum(1 for g in r["games"] if any(f.startswith(("CLAIR>","CLAIR-LOSS")) for f in g["flags"]))
            shortnc = sum(1 for g in r["games"] if any(f.startswith(("NC>human","NC-LOSS")) for f in g["flags"]))
            print("%-14s %4d %7.3f %7.3f %7s   clair %d/%d, NC %d/%d" % (
                r["deck"], r["n"], r["lp"]["human"], r["lp"]["clair"],
                "  -  " if args.no_nc else "%7.3f" % r["lp"]["nc"],
                short, r["n"], shortnc, r["n"]))
    if args.out:
        os.makedirs(args.out, exist_ok=True)
        json.dump(results, open(os.path.join(args.out, "summary.json"), "w"), indent=1)
        print("\nwrote %s/summary.json" % args.out)


if __name__ == "__main__":
    main()

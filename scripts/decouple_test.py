#!/usr/bin/env python3
"""Clairvoyance decouple test (MTG_SHUFFLE_SALT_SEARCH): does a policy read mid-game shuffles
(Ponder keep/reorder, cascade, ...) to gain a clairvoyant edge?

Coupled = search evaluates the SAME order the executor deals (default). Decoupled = search evaluates
salt A, executor deals salt B, so any shuffle-order the search "saw" is not the one played. A CLAIRVOYANT
policy loses quality coupled->decoupled (that drop = the clairvoyance it was exploiting). A genuinely
NON-clairvoyant policy is flat. Runs heuristic d5 (clairvoyant reference) and NC-d1 side by side.

    scripts/decouple_test.py --games 30 --seed 2002 --threads 2
"""
import argparse, os, re, subprocess, sys, time

MTG = "build/Release/mtg"
DECKS = {  # deck, max_turns
    "Hinata":   ("decks/Hinata2.cod", 10),   # Ponder (within-turn shuffle/reorder)
    "antilife": ("decks/Anti-Lifegain.cod", 8),  # fetchlands (within-turn shuffle); has keep profile
    "TH":       ("decks/treasure_hunt.txt", 8),
}
# (label, executor salt, search-eval salt). None search salt = lockstep (coupled).
COUPLINGS = [
    ("coupled",     "0",    None),
    ("decoupled-A", "0",    "7777"),
    ("decoupled-B", "5555", "3333"),
    ("decoupled-C", "9001", "1234"),
]
POLICIES = [
    # NC turn policy at the candidate teacher depths. A genuinely non-clairvoyant policy is FLAT
    # coupled->decoupled. blind = also reshuffle the lookahead mulligan bottomer (isolates the turn
    # policy on profile-less decks like Hinata; inert where a keep profile exists, e.g. antilife).
    ("NC-d1",        5, {"MTG_NC_SEARCH":"1","MTG_NC_K":"8","MTG_NC_DEPTH":"1"}),
    ("NC-d2",        5, {"MTG_NC_SEARCH":"1","MTG_NC_K":"8","MTG_NC_DEPTH":"2"}),
    ("NC-d1-blind",  5, {"MTG_NC_SEARCH":"1","MTG_NC_K":"8","MTG_NC_DEPTH":"1","MTG_NC_BLIND_BOTTOM":"1"}),
]


def run(deck, depth, games, seed, mt, threads, extra, exec_salt, search_salt):
    env = dict(os.environ)
    for k in ("MTG_EVAL_MODEL","MTG_VALUE_MODEL","MTG_NC_SEARCH","MTG_NC_K","MTG_NC_DEPTH",
              "MTG_NC_TEMPO","MTG_NC_TEMPO_LANDS","MTG_SHUFFLE_SALT","MTG_SHUFFLE_SALT_SEARCH",
              "MTG_NC_BLIND_BOTTOM"):
        env.pop(k, None)
    env["MTG_SHUFFLE_SALT"] = exec_salt
    if search_salt is not None:
        env["MTG_SHUFFLE_SALT_SEARCH"] = search_salt
    env.update(extra)
    t0 = time.time()
    out = subprocess.run([MTG, deck, "--games", str(games), "--seed", str(seed),
                          "--depth", str(depth), "--max-turns", str(mt), "--threads", str(threads)],
                         capture_output=True, text=True, env=env).stdout
    dt = time.time() - t0
    p = int(re.search(r"Games played\s*:\s*(\d+)", out).group(1))
    w = int(re.search(r"Games won\s*:\s*(\d+)", out).group(1))
    m = re.search(r"Avg win turn\s*:\s*([\d.]+)", out)
    a = float(m.group(1)) if m else float("nan")
    lp = (w*a + (p-w)*(mt+1)) / p
    return p, w, a, lp, 1000.0*dt/p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--games", type=int, default=30)
    ap.add_argument("--seed", type=int, default=2002)
    ap.add_argument("--threads", type=int, default=2)
    ap.add_argument("--decks", nargs="+", default=list(DECKS))
    ap.add_argument("--out", default="logs/eval/decouple_test.txt")
    args = ap.parse_args()
    of = open(args.out, "a")
    def emit(s):
        print(s); of.write(s+"\n"); of.flush()
    emit("\n===== DECOUPLE TEST  games=%d seed=%d threads=%d =====" % (args.games, args.seed, args.threads))
    emit("%-8s %-14s %-12s %6s %8s %8s %9s" % ("deck","policy","coupling","won","avg_won","LP","ms/game"))
    for dname in args.decks:
        deck, mt = DECKS[dname]
        for pname, depth, ee in POLICIES:
            base_lp = None
            for cname, esalt, ssalt in COUPLINGS:
                try:
                    p,w,a,lp,mspg = run(deck, depth, args.games, args.seed, mt, args.threads, ee, esalt, ssalt)
                except Exception as e:
                    emit("%-8s %-14s %-12s  ERROR %s" % (dname,pname,cname,e)); continue
                if cname == "coupled": base_lp = lp
                d = "" if base_lp is None or cname=="coupled" else "  dLP=%+.3f" % (lp-base_lp)
                emit("%-8s %-14s %-12s %4d/%-4d %8.3f %8.3f %9.1f%s" % (dname,pname,cname,w,p,a,lp,mspg,d))
    of.close()


if __name__ == "__main__":
    main()

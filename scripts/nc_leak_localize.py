#!/usr/bin/env python3
"""Localize the NC clairvoyance leak: is it the depth>0 honest CONTINUATION or the enumeration?

decoupled-A holds the EXECUTOR salt fixed (0) and only changes the SEARCH-eval salt (shuffle_salt_search).
A salt-INDEPENDENT NC path -> decoupled-A == coupled (dLP 0). ReshuffleAvgChoosePlan reshuffles per-k with
its own rs and sets s.shuffle_salt_search=rs, so it SHOULD be salt-independent -- yet d1 shows a delta.
d0 disables the honest-teacher continuation (HonestTeacherGuard(depth>0)). If d0 is flat and d1 leaks, the
leak is in the depth>0 continuation reading state.shuffle_salt_search on a state that isn't rs-stamped.

    scripts/nc_leak_localize.py --games 24 --seed 2002 --threads 3
"""
import argparse, os, re, subprocess, time

MTG = "build/Release/mtg"
DECK, MT = "decks/Hinata2.cod", 10


def run(depth, games, seed, threads, exec_salt, search_salt):
    env = dict(os.environ)
    for k in ("MTG_EVAL_MODEL","MTG_VALUE_MODEL","MTG_NC_SEARCH","MTG_NC_K","MTG_NC_DEPTH",
              "MTG_NC_TEMPO","MTG_NC_TEMPO_LANDS","MTG_SHUFFLE_SALT","MTG_SHUFFLE_SALT_SEARCH"):
        env.pop(k, None)
    env["MTG_NC_SEARCH"] = "1"; env["MTG_NC_K"] = "8"; env["MTG_NC_DEPTH"] = str(depth)
    env["MTG_SHUFFLE_SALT"] = exec_salt
    if search_salt is not None:
        env["MTG_SHUFFLE_SALT_SEARCH"] = search_salt
    t0 = time.time()
    out = subprocess.run([MTG, DECK, "--games", str(games), "--seed", str(seed),
                          "--depth", "5", "--max-turns", str(MT), "--threads", str(threads)],
                         capture_output=True, text=True, env=env).stdout
    dt = time.time() - t0
    p = int(re.search(r"Games played\s*:\s*(\d+)", out).group(1))
    w = int(re.search(r"Games won\s*:\s*(\d+)", out).group(1))
    m = re.search(r"Avg win turn\s*:\s*([\d.]+)", out)
    a = float(m.group(1)) if m else float("nan")
    lp = (w*a + (p-w)*(MT+1)) / p
    return p, w, a, lp, 1000.0*dt/p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--games", type=int, default=24)
    ap.add_argument("--seed", type=int, default=2002)
    ap.add_argument("--threads", type=int, default=3)
    ap.add_argument("--out", default="logs/eval/nc_leak_localize.txt")
    args = ap.parse_args()
    of = open(args.out, "a")
    def emit(s):
        print(s, flush=True); of.write(s+"\n"); of.flush()
    emit("\n===== NC LEAK LOCALIZE  games=%d seed=%d threads=%d =====" % (args.games, args.seed, args.threads))
    emit("%-8s %-12s %6s %8s %8s %9s" % ("policy","coupling","won","avg_won","LP","ms/game"))
    for depth in (0, 1):
        base = None
        for cname, ss in (("coupled", None), ("decoupled-A", "7777")):
            p,w,a,lp,mspg = run(depth, args.games, args.seed, args.threads, "0", ss)
            if cname == "coupled": base = lp
            d = "" if base is None or cname == "coupled" else "  dLP=%+.3f" % (lp-base)
            emit("NC-d%d    %-12s %4d/%-4d %8.3f %8.3f %9.1f%s" % (depth,cname,w,p,a,lp,mspg,d))
    of.close()


if __name__ == "__main__":
    main()

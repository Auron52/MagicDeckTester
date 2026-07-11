#!/usr/bin/env python3
"""ADOPTION decision by the PRIMARY metric: loss-penalised avg win turn (loss = max_turns+1), per user.
Compares, per deck / seed / depth, the pure heuristic leaf (value OFF) vs the adoption config value-leaf
(MTG_VALUE_MODEL + MIN_DEPTH=5 + STARTGATE_ALPHA=8). Uses the committed byte-identical-off binary and turns
the value leaf on purely by ENV -- no source flip (so it never contaminates a shared build tree).

dLP = value-on LP - value-off LP  (negative = value-leaf BETTER by the loss=9 avg-win-turn metric).
Also prints won/played and avg-win-turn so a win-count shift is visible. See docs/design/learned-d0-policy.md.

    scripts/valueleaf_adopt_lp.py --games 250 --seeds 1001 2002 3003 --depths 3 5 --threads 6
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
BUD = {3: 10, 5: 20}


def run(deck, depth, games, seed, mt, threads, profile, budget, value_on):
    env = dict(os.environ)
    for k in ("MTG_EVAL_MODEL","MTG_EVAL_PROFILE","MTG_VALUE_MODEL","MTG_VALUE_PROFILE",
              "MTG_NC_SEARCH","MTG_VALUE_MIN_DEPTH","MTG_VALUE_REDO_MODE","MTG_VALUE_STARTGATE_ALPHA"):
        env.pop(k, None)
    if value_on:
        env["MTG_VALUE_MODEL"] = "1"; env["MTG_VALUE_PROFILE"] = profile
        env["MTG_VALUE_MIN_DEPTH"] = "5"; env["MTG_VALUE_STARTGATE_ALPHA"] = "8"
    else:
        env["MTG_VALUE_MODEL"] = "0"   # binary default is now ON; must disable EXPLICITLY for the OFF arm
    cmd = [MTG, deck, "--games", str(games), "--seed", str(seed),
           "--depth", str(depth), "--max-turns", str(mt), "--threads", str(threads)]
    if budget and budget > 0:
        cmd += ["--budget-ms", str(budget)]
    out = subprocess.run(cmd, capture_output=True, text=True, env=env).stdout
    p = int(re.search(r"Games played\s*:\s*(\d+)", out).group(1))
    w = int(re.search(r"Games won\s*:\s*(\d+)", out).group(1))
    m = re.search(r"Avg win turn\s*:\s*([\d.]+)", out)
    a = float(m.group(1)) if m else float("nan")
    lp = (w*a + (p-w)*(mt+1)) / p
    return w, p, a, lp


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--games", type=int, default=250)
    ap.add_argument("--seeds", nargs="+", type=int, default=[1001, 2002, 3003])
    ap.add_argument("--depths", nargs="+", type=int, default=[3, 5])
    ap.add_argument("--budget-override", type=int, default=None,
                    help="force this budget-ms for all depths (0=unbounded); default uses BUD[depth]")
    ap.add_argument("--threads", type=int, default=6)
    ap.add_argument("--decks", nargs="+", default=list(DECKS))
    ap.add_argument("--out", default="logs/eval/valueleaf_adopt_lp.txt")
    args = ap.parse_args()
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    of = open(args.out, "a")
    def emit(s):
        print(s, flush=True); of.write(s+"\n"); of.flush()
    emit("\n===== ADOPTION LP (loss=mt+1)  value-OFF vs value-ON(K5,a8)  games=%d =====" % args.games)
    emit("%-8s %2s %5s   %14s   %14s   %s" % ("deck","d","seed", "OFF won/LP", "ON won/LP", "dLP"))
    net = {}
    for dname in args.decks:
        deck, prof, mt = DECKS[dname]
        for depth in args.depths:
            b = args.budget_override if args.budget_override is not None else BUD.get(depth, 0)
            for seed in args.seeds:
                try:
                    w0,p0,a0,lp0 = run(deck, depth, args.games, seed, mt, args.threads, prof, b, False)
                    w1,p1,a1,lp1 = run(deck, depth, args.games, seed, mt, args.threads, prof, b, True)
                    d = lp1 - lp0
                    net[(dname,depth)] = net.get((dname,depth), 0.0) + d
                    flag = "  <== WORSE" if d > 1e-9 else ("  better" if d < -1e-9 else "")
                    emit("%-8s d%d %5d   %4d/%7.4f   %4d/%7.4f   %+.4f%s" % (
                        dname, depth, seed, w0, lp0, w1, lp1, d, flag))
                except Exception as e:
                    emit("%-8s d%d %5d  ERROR %s" % (dname, depth, seed, e))
    emit("--- net dLP summed over seeds (per deck/depth) ---")
    for (dname,depth), d in sorted(net.items()):
        emit("  %-8s d%d   sum dLP=%+.4f%s" % (dname, depth, d, "  <== NET WORSE" if d > 1e-9 else ""))
    of.close()


if __name__ == "__main__":
    main()

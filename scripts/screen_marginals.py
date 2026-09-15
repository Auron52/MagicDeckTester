#!/usr/bin/env python3
"""Re-centre a finished screen on a REFERENCE arm, and diff its formats.

`deck_compare.py` reports every arm against `base`, which is the right frame when the question is
"is this edit an improvement". It is the wrong frame once a list is settled and the question becomes
"what is each CARD in it worth": for that you want every arm measured against the SETTLED list, so a
one-slot perturbation reads directly as that slot's marginal value.

The data for that is already in the screen's `.err` log -- every arm's per-game win turn, on the same
game indices -- so this re-centres rather than re-runs. Nothing here costs a game.

    python3 scripts/screen_marginals.py logs/deckcmp/<spec>.json --ref final
    python3 scripts/screen_marginals.py logs/deckcmp/<spec>.json --ref final --dist base,final

Three things it prints that the screen cannot:

  1. delta vs the REFERENCE arm, with its own se/t -- the screen's pairwise matrix has the point
     estimate but no error bar, and an eyeballed difference of two deltas-against-base has neither.
  2. the CROSS-FORMAT difference of those marginals, paired per game index. Both formats run the same
     seeds and the same `deck_numbering`, so a game index opens on the same seven cards in both
     worlds; pairing removes the opening-hand variance that dominates an unpaired comparison.
  3. the win-turn DISTRIBUTION, because an average turn hides which tail moved -- and in a lifegain
     deck measured across two life totals, that is exactly the question.

SIGN CONVENTION. Win turn is the objective, so negative is faster and better, and that is preserved
here: a `delta vs ref` of +0.04 means that arm is 0.04 turns SLOWER than the reference. For an arm
that CUT a copy, the value of the copy is therefore the POSITIVE of that delta, and the `worth`
column states it that way round so the reading does not depend on remembering which arm cut what.
"""
import argparse, json, math, os, re, sys
import statistics as st

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read_wins(path, max_turns):
    """{job name: {game index: win turn}}. An unwon game prints wt=-1 and scores max_turns+1 --
    the same convention deck_compare.score() uses, and dropping those games would bias the mean
    toward the arms that fail to win (they would simply contribute fewer samples)."""
    got = {}
    for line in open(path):
        m = re.match(r"\[win\] job=(\S+) gi=(\d+) wt=(-?\d+)", line)
        if m:
            wt = int(m.group(3))
            got.setdefault(m.group(1), {})[int(m.group(2))] = max_turns + 1 if wt < 0 else wt
    return got


def paired(got, a, b, idx):
    D = [got[b][g] - got[a][g] for g in idx]
    n = len(D)
    if not n:
        return float("nan"), float("nan"), 0, 0.0
    return (st.mean(D), st.pstdev(D) / math.sqrt(n), n,
            100 * sum(1 for x in D if x == 0) / n)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("spec", help="the screen's spec .json (its log is found beside it)")
    ap.add_argument("--ref", default="base", help="arm to re-centre on (default: base)")
    ap.add_argument("--label", default="screen", help="screen | confirm")
    ap.add_argument("--dist", default="", help="comma-separated arms to print win-turn histograms for")
    a = ap.parse_args()

    spec = json.load(open(a.spec))
    stem = os.path.splitext(os.path.basename(a.spec))[0]
    name = os.path.splitext(os.path.basename(spec["base"]))[0]
    out  = os.path.join(ROOT, "logs", "deckcmp", name)
    err  = os.path.join(out, f"{stem}.{a.label}.err")
    if not os.path.exists(err):
        raise SystemExit(f"no screen log at {err}")
    max_turns = int(spec.get("max_turns", 8))
    formats = list(spec.get("formats") or {"": {}})
    arms    = ["base"] + list(spec["combinations"])
    if a.ref not in arms:
        raise SystemExit(f"--ref wants one of: {', '.join(arms)}")

    jname = lambda f, t: t if not f else f"{f}::{t}"
    got = read_wins(err, max_turns)
    missing = [jname(f, t) for f in formats for t in arms if jname(f, t) not in got]
    if missing:
        raise SystemExit(f"the log is missing {len(missing)} job(s): {', '.join(missing[:6])}")
    # Pair on the games EVERY arm finished, so one short cell cannot silently give different arms
    # different denominators.
    idx = sorted(set.intersection(*[set(got[jname(f, t)]) for f in formats for t in arms]))
    print(f"{os.path.relpath(err, ROOT)}\n{len(idx):,} games per cell, re-centred on '{a.ref}'"
          f"   (negative = FASTER than {a.ref})\n")

    res = {}
    for f in formats:
        r = res[f] = {}
        ref = jname(f, a.ref)
        print(f"=== {f or 'single format'} ===   {a.ref} avg {st.mean([got[ref][g] for g in idx]):.4f}\n")
        print(f"  {'arm':14s} {'avg':>8s} {'vs ' + a.ref:>10s} {'se':>7s} {'t':>7s} {'ident':>7s}"
              f" {'worth':>9s}")
        for t in arms:
            d, se, n, ident = paired(got, ref, jname(f, t), idx)
            r[t] = {"delta": d, "se": se, "ident": ident}
            # `worth` states the same number from the CARD's point of view rather than the arm's: an
            # arm that cut a copy and came out slower means the copy was worth that much speed.
            worth = "" if t == a.ref else f"{-d:+9.4f}"
            print(f"  {t:14s} {st.mean([got[jname(f, t)][g] for g in idx]):8.4f} {d:+10.4f} {se:7.4f}"
                  f" {d/se if se else float('nan'):+7.2f} {ident:6.1f}% {worth:>9s}")
        print()

    if len(formats) > 1:
        f0, f1 = formats[0], formats[-1]
        print(f"=== {f1} minus {f0}, both re-centred on {a.ref} ===")
        print("  difference-of-differences, PAIRED on the game index (same seed, same numbering, so")
        print("  both formats open on the same hand).\n")
        # Stated in WORTH, not in delta, and the distinction is not cosmetic. Most arms here CUT a
        # copy, so their delta is the COST OF LOSING it -- and "this arm's delta shrank in 2HG" then
        # means the card got LESS important, which is the opposite of what the sentence sounds like.
        # Worth (= -delta) is always about the CARD, so one sign rule covers cut arms and add arms
        # alike: positive = the edit buys speed, and a positive diff = it buys more of it in f1.
        print(f"  {'arm':14s} {'worth ' + f0:>13.13s} {'worth ' + f1:>13.13s} {'diff':>10s}"
              f" {'se':>7s} {'t':>7s}")
        for t in arms:
            if t == a.ref:
                continue
            D = [(got[jname(f0, t)][g] - got[jname(f0, a.ref)][g])
                 - (got[jname(f1, t)][g] - got[jname(f1, a.ref)][g]) for g in idx]
            gap = st.mean(D)
            se = st.pstdev(D) / math.sqrt(len(D))
            print(f"  {t:14s} {-res[f0][t]['delta']:+13.4f} {-res[f1][t]['delta']:+13.4f}"
                  f" {gap:+10.4f} {se:7.4f} {gap/se if se else float('nan'):+7.2f}")
        print(f"\n  positive diff = this edit is worth MORE in {f1}; negative = worth more in {f0}.")
        print(f"  For an `m_*` (cut) arm, remember `worth` is the value of the arm's EDIT, so a")
        print(f"  negative worth means the copy it cut was earning its slot.\n")

    for t in [x.strip() for x in a.dist.split(",") if x.strip()]:
        if t not in arms:
            raise SystemExit(f"--dist wants arms of this spec: {t} is not one")
        print(f"=== win-turn distribution: {t} ===")
        lo = min(min(got[jname(f, t)].values()) for f in formats)
        print(f"  {'turn':>6s}" + "".join(f"{f or 'games':>20.20s}" for f in formats))
        for turn in range(lo, max_turns + 2):
            row = f"  {turn if turn <= max_turns else 'unwon':>6}"
            for f in formats:
                c = sum(1 for g in idx if got[jname(f, t)][g] == turn)
                row += f"{c:12,} {100*c/len(idx):6.2f}%"
            print(row)
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())

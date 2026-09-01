#!/usr/bin/env python3
"""BEHAVIOURAL diff of two cleanup-discard rule arms, from MTG_TRACE=discard streams.

WHY THIS EXISTS, and why it is not optional. An outcome A/B tells you a number; it cannot tell you
WHAT the rule did differently, and on this deck the outcome number alone hid a real defect for a
whole round -- a value term that had degenerated to distance-only sorting still measured
"about neutral", and only the diff showed it shedding the card the deck most wanted to keep. Run
this BEFORE reading any win-turn delta.

THE PAIRING RULE. Two arms play identical games only until the first decision they disagree on;
after that the hands, draws and boards diverge and comparing further decisions compares unrelated
states. So this pairs decisions within a game_seed BY POSITION and STOPS at the first mismatch,
counting that one as the divergence. "aligned" decisions are the honest denominator: decisions both
arms actually faced in the same state.

Input lines (stderr of a run with MTG_TRACE=discard):
    [discard] g<seed> T<turn> hand=.. cands=.. lip=.. landsinhand=.. tower=.. dropopen=.. -> <card>

Usage:  discard_behaviour_diff.py <base.txt> <arm.txt> [arm2.txt ...]
"""
import collections
import pathlib
import re
import sys

LINE_RE = re.compile(
    r"^\[discard\] g(?P<seed>\d+) T(?P<turn>\d+) hand=(?P<hand>\d+) cands=(?P<cands>\d+) "
    r"lip=(?P<lip>\d+) landsinhand=(?P<lih>\d+) tower=(?P<tower>\d+) dropopen=(?P<drop>\d+) "
    r"-> (?P<card>.+)$"
)


def load(path):
    """-> {game_seed: [decision, ...]} in emission order."""
    games = collections.defaultdict(list)
    for line in pathlib.Path(path).read_text().splitlines():
        m = LINE_RE.match(line.strip())
        if not m:
            continue
        games[int(m["seed"])].append({
            "turn": int(m["turn"]), "hand": int(m["hand"]), "cands": int(m["cands"]),
            "lip": int(m["lip"]), "card": m["card"],
        })
    return games


def diff(base, arm):
    aligned = changed = 0
    # A decision is only comparable while the two arms are still in the same game. `cands == 1` is
    # a forced discard -- the rule had no choice -- so it is reported separately: an arm cannot be
    # credited or blamed for a decision it did not make.
    forced = 0
    swaps = collections.Counter()
    contexts = []
    for seed, bd in base.items():
        ad = arm.get(seed)
        if ad is None:
            continue
        for b, a in zip(bd, ad):
            aligned += 1
            if b["cands"] <= 1:
                forced += 1
            if b["card"] == a["card"]:
                continue
            changed += 1
            swaps[(b["card"], a["card"])] += 1
            if len(contexts) < 25:
                contexts.append(
                    f"    g{seed} T{b['turn']} hand={b['hand']} cands={b['cands']} "
                    f"lip={b['lip']}:  {b['card']}  ->  {a['card']}")
            break   # everything after this in THIS game is a different game
    return aligned, forced, changed, swaps, contexts


def main():
    base_path, *arm_paths = sys.argv[1:]
    base = load(base_path)
    print(f"baseline {base_path}: {len(base)} games, "
          f"{sum(len(v) for v in base.values())} decisions")
    for ap in arm_paths:
        arm = load(ap)
        aligned, forced, changed, swaps, contexts = diff(base, arm)
        pct = 100.0 * changed / aligned if aligned else 0.0
        print(f"\n=== {pathlib.Path(ap).stem} vs {pathlib.Path(base_path).stem} ===")
        print(f"  aligned decisions : {aligned}  (forced, cands<=1: {forced})")
        print(f"  CHANGED           : {changed}  ({pct:.2f}% of aligned)")
        if not changed:
            print("  the arm never changed a decision -- nothing has been measured.")
            continue
        print("  swaps (baseline -> arm), most common first:")
        for (b, a), k in swaps.most_common(15):
            print(f"    {k:>5}x  {b}  ->  {a}")
        print("  first divergences:")
        for c in contexts:
            print(c)


if __name__ == "__main__":
    main()

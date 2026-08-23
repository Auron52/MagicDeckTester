#!/usr/bin/env python3
"""Per-deck check for the horizon-honest no-win tie-break (DecisionProvider::GradesNoWinLeaf).

WHY THIS EXISTS. The tie-break ships DEFAULT ON, because a per-deck opt-in means every new deck
silently keeps a blindness we know how to fix until somebody remembers to look. But it has ONE
predictable failure mode, and a deck that hits it must opt out:

    Opponent life at the horizon is a DAMAGE-RACE proxy. A deck whose value is STORED rather than
    expressed as damage by the horizon -- combo, storm, ramp, anything with a discontinuous payoff --
    has its build-up priced at ZERO, because rituals and enablers lower nobody's life on the turn
    they are cast. Dragonstorm is the measured instance: the tie-break truncated a 13-spell turn-6
    chain to six spells and turned a turn-8 win into a loss (s5005 gi227).

So the per-deck decision is a MEASUREMENT, not a judgement call -- which is what this automates.
Run it when onboarding a deck, or after play logic changes materially. See
docs/design/horizon-honest-leaf.md and .claude/skills/analyze-deck.md.

    python3 scripts/leaf_tiebreak_check.py decks/<Name>/<Name>.cod [--games 300] [--quick]

It runs BOTH arms in ONE pooled batch (per-job heurarm flags), train seeds against held-out seeds,
and prints a verdict. It never edits code: adopting an opt-out is a one-line provider override you
make deliberately.
"""
import argparse
import collections
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TRAIN = (1001, 2002, 3003)
HELD_OUT = (4004, 5005, 6006, 7007)


def build_manifest(deck, profile, games_d3, games_d5, path, seeds):
    jobs = []
    for arm, flags in (("base", {"MTG_LEAF_GRADE_NOWIN": False}),
                       ("leaf", {"MTG_LEAF_GRADE_NOWIN": True})):
        for depth, budget, games in ((3, 10, games_d3), (5, 20, games_d5)):
            for seed in seeds:
                jobs.append({"name": f"{arm}_d{depth}_s{seed}", "deck": deck, "profile": profile,
                             "games": games, "seed": seed, "depth": depth, "budget_ms": budget,
                             "ignore_play_profile": True, "weight": 0, "flags": flags})
    json.dump({"jobs": jobs}, open(path, "w"), indent=1)
    return sum(j["games"] for j in jobs)


def parse(out_path):
    pat = re.compile(r"\[win\] job=(\S+) gi=(\d+) wt=(-?\d+)")
    res = collections.defaultdict(dict)
    for line in open(out_path):
        m = pat.search(line)
        if m:
            arm, cell = m.group(1).split("_", 1)
            res[(arm, cell)][int(m.group(2))] = int(m.group(3))
    return res


def compare(res, seeds):
    """-> (net turns, worse, better, games). Positive net = the tie-break is WORSE."""
    net = worse = better = games = 0
    for depth in ("d3", "d5"):
        for seed in seeds:
            cell = f"{depth}_s{seed}"
            a, b = res.get(("base", cell)), res.get(("leaf", cell))
            if not a or not b:
                continue
            games += len(a)
            for gi in a:
                # THE metric: loss-penalised win turn, unwon = max_turns+1 = 9.
                d = (9 if b[gi] < 0 else b[gi]) - (9 if a[gi] < 0 else a[gi])
                if d:
                    net += d
                    worse += d > 0
                    better += d < 0
    return net, worse, better, games


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("deck")
    ap.add_argument("--profile", default=None, help="defaults to <deck stem>.profile.json")
    ap.add_argument("--games", type=int, default=300, help="games per d3 cell (d5 uses 2/3 of it)")
    ap.add_argument("--quick", action="store_true", help="train seeds only, 100 games/cell")
    args = ap.parse_args()

    profile = args.profile or (os.path.splitext(args.deck)[0] + ".profile.json")
    for p in (args.deck, profile):
        if not os.path.exists(os.path.join(ROOT, p)):
            sys.exit(f"not found: {p}")

    g3 = 100 if args.quick else args.games
    g5 = max(1, g3 * 2 // 3)
    manifest = "/tmp/leaf_tiebreak_check.json"
    all_seeds = TRAIN if args.quick else TRAIN + HELD_OUT  # --quick must not RUN cells it won't report
    total = build_manifest(args.deck, profile, g3, g5, manifest, all_seeds)
    binary = os.path.join(ROOT, "build/Release/mtg")
    if not os.path.exists(binary):
        sys.exit("build/Release/mtg missing -- run ./build.sh first")

    print(f"{total} games, both arms in one pooled batch ...")
    out = "/tmp/leaf_tiebreak_check.out"
    env = dict(os.environ, MTG_DUMP_WINS="1")
    with open(out, "w") as fh:
        rc = subprocess.call([binary, "--batch", manifest], stdout=fh, stderr=subprocess.STDOUT,
                             cwd=ROOT, env=env)
    if rc != 0:
        sys.exit(f"batch failed (exit {rc}); see {out}")

    res = parse(out)
    splits = [("train", TRAIN)] if args.quick else [("train", TRAIN), ("held-out", HELD_OUT)]
    print(f"\n{'split':10s} {'games':>7s} {'net turns':>10s} {'worse':>6s} {'better':>7s}"
          "   (negative = the tie-break HELPS)")
    results = {}
    for label, split_seeds in splits:      # NOT `seeds`: shadowing it silently made ALL == held-out
        net, w, b, n = compare(res, split_seeds)
        results[label] = (net, w, b, n)
        print(f"{label:10s} {n:7d} {net:+10d} {w:6d} {b:7d}")
    net_all, w_all, b_all, n_all = compare(res, all_seeds)
    print(f"{'ALL':10s} {n_all:7d} {net_all:+10d} {w_all:6d} {b_all:7d}")

    print()
    if n_all and net_all == 0 and w_all == 0:
        print("VERDICT: INERT on this sample -- keep the default (ON). Nothing to decide.")
    elif net_all < 0:
        print("VERDICT: KEEP THE DEFAULT (ON). The tie-break helps this deck.")
    else:
        signs = {lbl: r[0] for lbl, r in results.items()}
        consistent = len(signs) > 1 and all(v > 0 for v in signs.values())
        print("VERDICT: CANDIDATE FOR OPT-OUT -- the tie-break costs this deck"
              + (" on BOTH splits." if consistent else " (one split only -- weak)."))
        print("  Before overriding GradesNoWinLeaf() to false, ESCALATE the regressed games on BOTH")
        print("  arms at +1/+2 depth AND 20x budget. Most regressions are ordinary budget churn and")
        print("  close at 20x; only a game that survives BOTH is a valuation failure worth an")
        print("  opt-out. Look for the stored-value signature: does the deck bank resources whose")
        print("  payoff lands beyond the horizon (rituals, ramp, combo pieces)? That is the one")
        print("  failure mode this hook exists for.")


if __name__ == "__main__":
    main()

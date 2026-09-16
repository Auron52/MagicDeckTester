#!/usr/bin/env python3
"""Score a stompy_h2h.py pooled batch, reusing deck_compare's own score()/paired().

Not a re-implementation: `score()` REFUSES a job that did not finish every game (a truncated run
otherwise reads as a perfectly ordinary number), and scores an unwon game as max_turns+1 rather than
dropping it -- dropping is a silent bias toward the arm that loses more often.

Every pairing is within a format and across the two decks, on the intersection of game indices. Both
decks carry the number set {1..60}, so the same seed deals the same positions and only the CARD at a
number differs -- which is what makes a paired delta meaningful across two different decklists.
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import deck_compare as dc

ap = argparse.ArgumentParser()
ap.add_argument("--log", default="logs/stompy_h2h/h2h.out")
ap.add_argument("--decks", nargs="+", required=True)
ap.add_argument("--formats", nargs="+", default=["1v1", "2hg"])
ap.add_argument("--games", type=int, required=True)
ap.add_argument("--max-turns", type=int, default=10)
a = ap.parse_args()

arms = [f"{f}::{d}" for f in a.formats for d in a.decks]
got = dc.score(a.log, arms, a.max_turns, expect=a.games)

for f in a.formats:
    print(f"\n=== {f} ===")
    print(f"  {'deck':26s} {'avg win turn':>13s}   {'n':>9s}")
    for d in a.decks:
        v = got[f"{f}::{d}"]
        print(f"  {d:26s} {sum(v.values())/len(v):13.4f}   {len(v):9,}")
    base = a.decks[0]
    print(f"\n  paired vs {base} (negative = the row is FASTER):")
    print(f"  {'deck':26s} {'delta':>9s} {'se':>8s} {'t':>9s} {'identical':>10s}")
    for d in a.decks[1:]:
        m, se, n, ident = dc.paired(got, f"{f}::{base}", f"{f}::{d}")
        print(f"  {d:26s} {m:+9.4f} {se:8.4f} {m/se if se else 0:9.2f} {ident:9.1f}%")

# Same pair, the two formats compared -- a sign flip here is a real deckbuilding fact.
if len(a.formats) == 2 and len(a.decks) >= 2:
    f1, f2 = a.formats
    print(f"\n=== {f2} minus {f1} (does the format change the answer?) ===")
    base = a.decks[0]
    for d in a.decks[1:]:
        m1, _, _, _ = dc.paired(got, f"{f1}::{base}", f"{f1}::{d}")
        m2, _, _, _ = dc.paired(got, f"{f2}::{base}", f"{f2}::{d}")
        print(f"  {d:26s} {f1}={m1:+.4f}  {f2}={m2:+.4f}  diff={m2-m1:+.4f}")

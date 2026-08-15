#!/usr/bin/env python3
"""Phase C.5 -- decide whether the matrix's ABANDONED games could have changed its answers.

Runs between the matrix (phase C) and the derivation (phase D). The matrix is measured with a
per-game work ceiling, so its rows describe "games that COMPLETE within the ceiling", not all games.
That filter is normally harmless -- but it is not harmless by construction, and the cheap way to find
out is to ask the data rather than to raise the ceiling for everyone.

WHY THIS EXISTS (user, 2026-08-15): "condemn at 30 minutes and then have the pipeline consider
re-running at 1 hour if there are any risky cells... Or if there are too many condemnations I suppose
that could be another reason to raise it. However, in some cases it is best to defer back to the AI or
user for a decision."

THE MEASUREMENT. Every completed game is retained on disk (a skip is a FILTER at reduce time, not a
deletion), so for any pair of cells we can compute the same comparison twice:

  * UNION-FILTERED  -- the games the table actually reports: those no cell abandoned.
  * BOTH-COMPLETED  -- every game BOTH cells finished, including ones the union dropped because a
                       THIRD cell could not finish them.

If those two agree about which side of the tolerance a pair sits on, the filter did not decide that
verdict and there is nothing to fix. If they disagree, the filter IS the answer for that pair, and it
must be re-measured rather than trusted.

WHY PAIRED AND NOT ROW MEANS. A cell's raw mean is over its OWN completed games, and cells complete
different games, so comparing raw means compares different populations -- that is how a first pass at
this analysis produced a spurious "sign flip" and an apparent 64% compression of the depth ladder,
both of which vanished once the comparison was paired. Every number here is a paired difference over a
common game set.

EXIT CODES
  0  CLEAN  -- proceed to phase D.
  3  RERUN  -- risky cells found and they are cheap enough to re-measure; the command is printed.
  4  DEFER  -- the call is consequential (many condemned cells, or risky cells that are expensive to
               re-run). Stop and put it to a human. Deferring is not failure: raising the ceiling
               costs hours and lowering the sample costs correctness, and that trade is not the
               pipeline's to make silently.
"""
import argparse
import collections
import glob
import json
import math
import os
import re
import sys


def load(qdir):
    wins = collections.defaultdict(dict)          # (cell, seed) -> {offset: win_turn}
    for f in glob.glob(os.path.join(qdir, "wins", "*.wins")):
        m = re.search(r"([HV]\d+)_s(\d+)_off(\d+)\.wins$", f)
        if not m:
            continue
        for ln in open(f):
            p = ln.split()
            if len(p) >= 2:
                wins[(m.group(1), m.group(2))][int(p[0])] = int(p[1])
    ab = collections.defaultdict(set)             # seed -> union of abandoned global indices
    per_cell_ab = collections.Counter()
    for f in glob.glob(os.path.join(qdir, "wins", "*.abandoned")):
        m = re.search(r"([HV]\d+)_s(\d+)_", f)
        if not m:
            continue
        idx = [int(x) for x in open(f).read().split() if x.strip()]
        ab[m.group(2)].update(idx)
        per_cell_ab[(m.group(1), m.group(2))] += len(idx)
    dead = set()
    cj = os.path.join(qdir, "matrix.txt.cells.json")
    if os.path.exists(cj):
        d = json.load(open(cj))
        cells = d if isinstance(d, list) else d.get("cells", d)
        for x in cells:
            if x.get("intractable"):
                dead.add(("%s%d" % (x["arm"], x["depth"]), str(x["seed"])))
    return wins, ab, per_cell_ab, dead


def paired(wins, ab, a, b, seeds, filt):
    """Mean and SE of (a - b) over a COMMON game set."""
    ds = []
    for s in seeds:
        A = wins.get((a, s), {})
        B = wins.get((b, s), {})
        common = set(A) & set(B)
        if filt:
            common -= ab[s]
        ds.extend(A[o] - B[o] for o in common)
    if len(ds) < 2:
        return None
    n = len(ds)
    m = sum(ds) / n
    sd = math.sqrt(sum((x - m) ** 2 for x in ds) / (n - 1))
    return m, sd / math.sqrt(n), n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("qdir", help="the deck's value-leaf queue dir (logs/vlq_<deck>)")
    ap.add_argument("--tol", type=float, default=0.0020,
                    help="equivalence tolerance the derivation uses (must match phase D)")
    ap.add_argument("--abandon-warn", type=float, default=25.0,
                    help="per-cell abandonment %% above which a cell is risky on its own. The filter "
                         "reshapes a population rather than trimming a tail past roughly this rate.")
    ap.add_argument("--max-condemned", type=int, default=2,
                    help="condemned (intractable) cells above which the call goes to a human rather "
                         "than being auto-resolved")
    ap.add_argument("--rerun-budget-cells", type=int, default=6,
                    help="risky cells above which a re-run is too expensive to launch unattended")
    ap.add_argument("--higher-ceiling", type=int, default=40000000,
                    help="the ceiling a re-run would use, in work units (default ~1h; the 30-min "
                         "default is 20M -- see valueleaf.sh ABANDON_FLOOR_UNITS)")
    args = ap.parse_args()

    wins, ab, per_cell_ab, dead = load(args.qdir)
    if not wins:
        print("risk-gate: no .wins under %s -- nothing to check" % args.qdir)
        return 0
    seeds = sorted({s for _, s in wins})
    cells = sorted({c for c, _ in wins}, key=lambda x: (x[0], int(x[1:])))

    # ---------------------------------------------------------------- per-cell exposure
    print("=" * 78)
    print("MATRIX RISK GATE -- can the abandoned games have changed the answers?")
    print("=" * 78)
    print("\n%-5s %10s %10s %9s   %s" % ("cell", "completed", "abandoned", "aband %", "flag"))
    risky_cells = set()
    for c in cells:
        comp = sum(len(wins[(c, s)]) for s in seeds if (c, s) in wins)
        abn = sum(per_cell_ab[(c, s)] for s in seeds)
        pct = 100.0 * abn / (comp + abn) if comp + abn else 0.0
        flag = ""
        if pct >= args.abandon_warn:
            flag = "HIGH ABANDONMENT"
            risky_cells.add(c)
        print("%-5s %10d %10d %8.2f%%   %s" % (c, comp, abn, pct, flag))
    if dead:
        print("\ncondemned (intractable, zero games): %d" % len(dead))
        for c, s in sorted(dead):
            print("    %s seed %s" % (c, s))
            risky_cells.add(c)

    # ---------------------------------------------------------------- verdict stability
    print("\n%s\nVERDICT STABILITY at tol=%.4f  (does the filter decide any comparison?)\n%s"
          % ("-" * 78, args.tol, "-" * 78))
    print("%-12s %20s %20s   %s" % ("pair", "both-completed", "union-filtered", "note"))
    flips = []
    marginal = []
    unresolved = []
    for ladder in ("H", "V"):
        L = [c for c in cells if c[0] == ladder]
        for i, a in enumerate(L):
            for b in L[i + 1:]:
                u = paired(wins, ab, a, b, seeds, False)
                f = paired(wins, ab, a, b, seeds, True)
                if not u or not f:
                    continue
                vu = u[0] <= args.tol
                vf = f[0] <= args.tol
                note = ""
                # A point estimate crossing `tol` is NOT by itself evidence that the filter decided
                # anything. Both sides carry sampling error, and on the deep V cells the SE is an
                # order of magnitude larger than the gap being judged -- measured here, V4 vs V7 went
                # +0.0159 -> -0.0022 with an SE of +-0.0212, i.e. neither number is distinguishable
                # from zero OR from tol. Calling that "the filter decided it" would send a re-run at a
                # higher ceiling to fix a problem the ceiling cannot touch.
                #
                # So the two failure modes are separated, because they have OPPOSITE remedies:
                #   RESOLVED flip  -- both estimates are individually resolvable against tol and they
                #                     disagree. The filter really is the answer. More CEILING.
                #   UNRESOLVED     -- the comparison cannot be called either way at this sample.
                #                     More GAMES. Raising the ceiling buys nothing.
                resolvable = (abs(u[0] - args.tol) > u[1]) and (abs(f[0] - args.tol) > f[1])
                if vu != vf and resolvable:
                    note = "*** FILTER DECIDES THIS (resolved) ***"
                    flips.append((a, b))
                    risky_cells.update((a, b))
                elif vu != vf:
                    note = "unresolved: verdict flips but |diff| << SE -- needs GAMES, not ceiling"
                    unresolved.append((a, b))
                elif abs(f[0] - args.tol) <= f[1]:
                    note = "marginal (within 1 SE of tol)"
                    marginal.append((a, b))
                if note:
                    print("%-12s %+10.4f+-%.4f %+10.4f+-%.4f   %s"
                          % (a + " vs " + b, u[0], u[1], f[0], f[1], note))
    if not flips and not marginal and not unresolved:
        print("(every comparison lands the same side of tol either way)")

    # ---------------------------------------------------------------- the call
    print("\n" + "=" * 78)
    n_dead = len(dead)
    cellset = ",".join(sorted(risky_cells, key=lambda x: (x[0], int(x[1:]))))

    if not risky_cells and not flips and n_dead == 0 and not unresolved:
        print("CLEAN -- the abandoned games cannot have changed any verdict. Proceed to phase D.")
        return 0

    # A PILE OF CONDEMNATIONS IS NOT A CEILING PROBLEM (user, 2026-08-15: "If we have a ton of
    # condemnations it means we need more optimizations"). A condemned cell is one whose games could
    # not finish in an HOUR of wall clock; raising the ceiling to two hours would convert some of them
    # into games that merely take two hours. That buys a table at the price of a run nobody can repeat,
    # and it hides the real finding, which is that the engine is too slow for this deck at this depth.
    # So this case is reported as an ENGINE signal, not as a threshold to retune.
    if n_dead > args.max_condemned:
        print("DEFER TO A HUMAN -- %d condemned cell(s), which is an OPTIMIZATION signal, not a\n"
              "ceiling one. These cells could not finish a game inside the wall-clock limit at all;\n"
              "a bigger ceiling would only turn one-hour games into two-hour games and make the run\n"
              "less repeatable while hiding the cause." % n_dead)
        for c, s in sorted(dead):
            print("    condemned: %s seed %s" % (c, s))
        print("\nOptions:\n"
              "  (a) profile and optimise the engine for this deck's expensive depths, then re-run;\n"
              "  (b) reduce scope -- drop the depths that cannot be measured at this budget;\n"
              "  (c) accept a table with those cells reference-only, if the decisions do not read them.")
        return 4

    if flips and len(risky_cells) > args.rerun_budget_cells:
        print("DEFER TO A HUMAN -- %d comparison(s) are decided by the filter, across %d cells, which\n"
              "is more than can be re-measured unattended." % (len(flips), len(risky_cells)))
        print("  cells: %s" % cellset)
        return 4

    if flips:
        print("RERUN RECOMMENDED -- %d comparison(s) are DECIDED by the filter and the set is small\n"
              "enough to re-measure unattended." % len(flips))
        print("  cells:   %s" % cellset)
        print("  ceiling: %d units (~1h) instead of the 30-minute default" % args.higher_ceiling)
        return 3

    # Nothing is decided by the filter, but something is too noisy to call. Raising the ceiling is the
    # WRONG lever here and saying so is the point of this branch.
    if unresolved:
        print("DEFER TO A HUMAN -- %d comparison(s) flip sides of tol, but the gap is far smaller than\n"
              "its own sampling error, so the filter is not what decided them. MORE GAMES would settle\n"
              "these; a higher ceiling would not." % len(unresolved))
        for a, b in unresolved:
            print("    unresolved: %s vs %s" % (a, b))
        return 4

    print("PROCEED WITH A NOTE -- %d cell(s) flagged on abandonment rate alone, but no comparison\n"
          "changes verdict. The table measures games that complete; that is disclosed in it." % len(risky_cells))
    print("  cells: %s" % cellset)
    return 0


if __name__ == "__main__":
    sys.exit(main())

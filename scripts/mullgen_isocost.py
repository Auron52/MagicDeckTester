#!/usr/bin/env python3
"""Iso-cost analysis of mulligan-generation depth.

Reads the sweep's per-arm comp labels and turns them into the comparison that actually decides a
generation setting. The naive comparison -- "is d2 cheaper than d3?" -- is the wrong question twice
over:

  1. A mulligan profile is a RANKING. A labeller that is uniformly worse produces the identical keep
     policy, because keeps are comparisons between hands. So the quantity that matters is not the
     mean shift but how much the arm REORDERS hands relative to the reference.
  2. A cheaper arm can spend its saving on MORE ROLLOUTS. Its label error is
         err(arm)^2 = centered_bias(arm)^2 + per_rollout_var(arm) / R(arm),   R(arm) = T / cost(arm)
     so it trades bias UP for noise DOWN. Straight time saving is not the comparison; iso-cost is.

The reference is each deck's own SHIPPED PLAY settings, not a generic "strong" setting: labels exist
to rank hands for the policy the deck actually plays.
"""
import argparse
import collections
import glob
import math
import os
import random
import re
import sys

ARMS = ["d0b3", "d1b3", "d2b3", "d3b3", "d5b3", "d3b20", "d5b20", "ref"]


def read_tsv(path):
    """-> {comp_line: (dmean, dse, pmean, pse)}"""
    out = {}
    if not os.path.exists(path):
        return out
    for raw in open(path):
        parts = raw.rstrip("\n").split("\t")
        if len(parts) < 3:
            continue
        try:
            dm, ds = (float(x) for x in parts[1].split())
            pm, ps = (float(x) for x in parts[2].split())
        except ValueError:
            continue
        out[parts[0]] = (dm, ds, pm, ps)
    return out


def read_timing(path):
    """-> {(deck, arm): dict}"""
    out = {}
    if not os.path.exists(path):
        return out
    for raw in open(path):
        f = raw.rstrip("\n").split("\t")
        if len(f) < 7:
            continue
        out[(f[0], f[1])] = dict(depth=int(f[2]), budget=int(f[3]), wall=float(f[4]),
                                 rows=int(f[5]), rc=int(f[6]))
    return out


def read_cost_units(path):
    """-> {(deck, arm): units_per_rollout} from the deterministic cost probe."""
    out = {}
    if not os.path.exists(path):
        return out
    for raw in open(path):
        f = raw.rstrip("\n").split("\t")
        if len(f) < 5 or f[4] == "NA":
            continue
        try:
            out[(f[0], f[1])] = float(f[4])
        except ValueError:
            pass
    return out


def calibrate_baseline(units, walls, anchor="d3b3"):
    """Per-rollout NON-SEARCH cost, in work-unit-equivalents.

    Work units count SEARCH work only (they come off SearchBudget::Consume), so depth 0 -- which does
    no search at all -- measures exactly 0. Pricing it at 0 would make the cheapest arm look free.
    A rollout also pays a fixed cost to simulate the game, so total cost is modelled as

        cost(arm) = search_units(arm) + C          (C = the per-rollout baseline)

    and C is pinned from the one pair that isolates it: d0 (all baseline, no search) against an arm
    with substantial search. Wall RATIOS within a deck survive a loaded box (contention scales both
    arms alike), which is what makes this usable without an idle machine.
    """
    if "d0b3" not in walls or anchor not in walls or anchor not in units:
        return None
    w0, wa, ua = walls["d0b3"], walls[anchor], units[anchor]
    if w0 <= 0 or wa <= w0 or ua <= 0:
        return None
    return ua * w0 / (wa - w0)


def read_work(errpath):
    """Pull the deterministic work-unit counter out of an arm's stderr, if present."""
    if not os.path.exists(errpath):
        return None
    m = None
    for line in open(errpath):
        if line.startswith("[score]"):
            m = line
    if not m:
        return None
    d = dict(re.findall(r"(\w+)=([0-9.eE+-]+)", m))
    try:
        return dict(work=float(d["work_units"]), per_rollout=float(d["units_per_rollout"]),
                    rollouts=float(d["rollouts"]))
    except KeyError:
        return None


def spearman(a, b):
    n = len(a)
    if n < 3:
        return float("nan")

    def ranks(v):
        order = sorted(range(n), key=lambda i: v[i])
        r = [0.0] * n
        i = 0
        while i < n:
            j = i
            while j + 1 < n and v[order[j + 1]] == v[order[i]]:
                j += 1
            avg = (i + j) / 2.0 + 1.0
            for k in range(i, j + 1):
                r[order[k]] = avg
            i = j + 1
        return r

    ra, rb = ranks(a), ranks(b)
    ma, mb = sum(ra) / n, sum(rb) / n
    num = sum((x - ma) * (y - mb) for x, y in zip(ra, rb))
    da = math.sqrt(sum((x - ma) ** 2 for x in ra))
    db = math.sqrt(sum((y - mb) ** 2 for y in rb))
    return num / (da * db) if da > 0 and db > 0 else float("nan")


def pct(v, q):
    s = sorted(v)
    if not s:
        return float("nan")
    i = min(len(s) - 1, max(0, int(round(q * (len(s) - 1)))))
    return s[i]


def decision_agreement(arm, ref, qs=(0.25, 0.5, 0.75)):
    """Fraction of comps on which arm and ref agree about keep-vs-mull, at thresholds set to
    percentiles of the REFERENCE labels (the policy boundary the profile would actually draw)."""
    accs = []
    for q in qs:
        t = pct(ref, q)
        agree = sum(1 for x, y in zip(arm, ref) if (x <= t) == (y <= t))
        accs.append(agree / len(ref))
    return accs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default="logs/mullgen_iso")
    ap.add_argument("-R", type=int, default=40, help="rollouts per comp per side used in the sweep")
    ap.add_argument("--trials", type=int, default=240, help="noise draws for the iso-cost curve")
    ap.add_argument("--seed", type=int, default=7)
    args = ap.parse_args()

    timing = read_timing(os.path.join(args.dir, "timing.tsv"))
    costs = read_cost_units(os.path.join(args.dir, "cost_units.tsv"))
    decks = []
    for p in sorted(glob.glob(os.path.join(args.dir, "*.ref.tsv"))):
        decks.append(os.path.basename(p)[: -len(".ref.tsv")])
    if not decks:
        raise SystemExit("no *.ref.tsv in %s -- sweep has not produced a reference yet" % args.dir)

    rng = random.Random(args.seed)
    print("=" * 100)
    print("PER-DECK LABEL FIDELITY vs the deck's own shipped play settings (the reference)")
    print("  bias   = mean(arm - ref) in turns; a UNIFORM shift is harmless to a ranking")
    print("  rmsd_c = RMS of the CENTERED residual = the part that actually reorders hands")
    print("  rho    = Spearman rank correlation with the reference (1.000 = identical ranking)")
    print("  agree  = keep/mull agreement at ref quartile thresholds (25/50/75%)")
    print("  cost   = deterministic work units per rollout (contention-proof); wall in seconds")
    print("=" * 100)

    summary = collections.defaultdict(list)
    percomp = {}

    for deck in decks:
        ref = read_tsv(os.path.join(args.dir, "%s.ref.tsv" % deck))
        if not ref:
            continue
        print("\n### %s   (%d comps)" % (deck, len(ref)))
        print("%-7s %8s %8s %7s %-22s %10s %10s" %
              ("arm", "bias", "rmsd_c", "rho", "agree 25/50/75", "units/roll", "wall_s"))
        percomp[deck] = {}
        for arm in ARMS:
            cur = read_tsv(os.path.join(args.dir, "%s.%s.tsv" % (deck, arm)))
            if not cur:
                continue
            keys = [k for k in ref if k in cur]
            if len(keys) < 5:
                continue
            # combine on-the-draw and on-the-play (both halves feed the keep policy)
            a = [(cur[k][0] + cur[k][2]) / 2.0 for k in keys]
            r = [(ref[k][0] + ref[k][2]) / 2.0 for k in keys]
            sd = [((cur[k][1] + cur[k][3]) / 2.0) * math.sqrt(args.R) for k in keys]
            d = [x - y for x, y in zip(a, r)]
            bias = sum(d) / len(d)
            rmsd_c = math.sqrt(sum((x - bias) ** 2 for x in d) / len(d))
            rho = spearman(a, r)
            agree = decision_agreement(a, r)
            t = timing.get((deck, arm), {})
            # Prefer the sweep's OWN stderr counter: cost and quality then come from one run, at the
            # same R and the same comps, so they cannot drift apart. cost_units.tsv (a separate cheap
            # probe) is the fallback for older sweeps that predate the counter.
            w = read_work(os.path.join(args.dir, "%s.%s.err" % (deck, arm)))
            u = w["per_rollout"] if w else costs.get((deck, arm))
            percomp[deck][arm] = dict(a=a, r=r, sd=sd, rho=rho, bias=bias, rmsd_c=rmsd_c,
                                      search_units=u, wall=t.get("wall"))
            print("%-7s %+8.4f %8.4f %7.3f %-22s %10s %10s" %
                  (arm, bias, rmsd_c, rho,
                   "/".join("%.3f" % x for x in agree),
                   ("%.0f" % u) if u is not None else "-",
                   ("%.1f" % t["wall"]) if t else "-"))
            summary[arm].append(dict(deck=deck, rho=rho, rmsd_c=rmsd_c, bias=bias,
                                     search_units=u, wall=t.get("wall")))

        # total cost = search work + the per-rollout baseline that d0 isolates
        du = {k: v["search_units"] for k, v in percomp[deck].items() if v["search_units"] is not None}
        dw = {k: v["wall"] for k, v in percomp[deck].items() if v["wall"]}
        C = calibrate_baseline(du, dw)
        for arm, v in percomp[deck].items():
            v["units"] = (v["search_units"] + C) if (C is not None and v["search_units"] is not None) \
                else None
        if C is not None:
            print("  baseline C = %.0f units/rollout (non-search cost; d0's entire cost)" % C)

    # ---------------------------------------------------------------- ISO-COST
    # Give every arm the SAME total budget and let the cheap ones buy more rollouts. The budget is
    # anchored so the CHEAPEST arm gets exactly the sweep's R (we can add noise to simulate fewer
    # rollouts, never remove it to simulate more).
    print("\n" + "=" * 100)
    print("ISO-COST: same total spend per comp, cheap arms buy MORE rollouts")
    print("  R(arm) = T / cost(arm); label = measured mean + N(0, sd/sqrt(R(arm)))")
    print("  budget T is anchored at [cheapest arm gets R=%d]; multipliers scale T up." % args.R)
    print("=" * 100)

    for deck, arms in percomp.items():
        have = {k: v for k, v in arms.items() if v.get("units")}
        if len(have) < 2:
            print("\n### %s -- no work-unit data yet (rebuild with the [score] counter)" % deck)
            continue
        cheapest = min(have.values(), key=lambda v: v["units"])
        print("\n### %s" % deck)
        hdr = "  ".join("%-7s" % a for a in have)
        print("%-10s %s" % ("budget", hdr))
        for mult in (1, 4, 16, 64, 256, 1024):
            T = cheapest["units"] * args.R * mult
            cells = []
            for arm, v in have.items():
                R_a = max(1.0, T / v["units"])
                tot = 0.0
                for _ in range(args.trials):
                    noisy = [m + rng.gauss(0.0, s / math.sqrt(R_a)) for m, s in zip(v["a"], v["sd"])]
                    tot += spearman(noisy, v["r"])
                cells.append("%.3f" % (tot / args.trials))
            print("%-10s %s" % ("%dx" % mult, "  ".join("%-7s" % c for c in cells)))
        # Noise-free ceiling: the arm's rank fidelity once R is large enough that only BIAS remains.
        # (Measured at the sweep's R, so it still carries a little R-noise -- it is a LOWER bound on
        # the true ceiling, equally for every arm.)
        print("%-10s %s" % ("ceiling", "  ".join("%-7s" % ("%.3f" % v["rho"]) for v in have.values())))
        print("%-10s %s" % ("R@1x", "  ".join(
            "%-7s" % ("%.0f" % max(1.0, (cheapest["units"] * args.R) / v["units"]))
            for v in have.values())))

    # ---------------------------------------------------------------- CROSS-DECK
    print("\n" + "=" * 100)
    print("CROSS-DECK SUMMARY")
    print("=" * 100)
    print("%-7s %6s %9s %9s %12s" % ("arm", "decks", "mean_rho", "mean_rmsd", "mean_cost_x"))
    base = {d: percomp[d]["d3b3"]["units"] for d in percomp
            if "d3b3" in percomp[d] and percomp[d]["d3b3"].get("units")}
    for arm in ARMS:
        rows = summary.get(arm, [])
        if not rows:
            continue
        rh = [r["rho"] for r in rows if r["rho"] == r["rho"]]
        rm = [r["rmsd_c"] for r in rows]
        cx = [percomp[r["deck"]][arm]["units"] / base[r["deck"]] for r in rows
              if base.get(r["deck"]) and percomp[r["deck"]].get(arm, {}).get("units")]
        print("%-7s %6d %9.4f %9.4f %12s" %
              (arm, len(rows), sum(rh) / max(1, len(rh)), sum(rm) / max(1, len(rm)),
               ("%.3f" % (sum(cx) / len(cx))) if cx else "-"))
    print("\n(cost_x is relative to d3b3 = 1.000, in deterministic work units)")


if __name__ == "__main__":
    main()

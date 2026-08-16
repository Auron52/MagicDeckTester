#!/usr/bin/env python3
"""Paired A/B report over CHUNKED jobs, WITH the resolution floor the old reporter lacks.

Two things this does that scripts/vlq_ab_report.py does not:

  1. AGGREGATES CHUNKS. Jobs are emitted as small chunks (`<arm>_s<seed>_off<n>`) so no single slow
     game can strand the rest of a 1000-game job behind it (see
     docs/design/phase-e-job-granularity.md). Chunks are folded back to one (arm, seed) cell by a
     game-weighted mean before anything is compared.

  2. APPLIES A RESOLUTION FLOOR. Win turns are INTEGERS, so the smallest observable change in a
     seed's mean is one game moving by one turn = 1/games_per_seed. A reported delta below the
     sample's resolution is not a small effect, it is NO MEASUREMENT -- and the old reporter printed
     one anyway. Mirrorwing 2026-08-16: live vs staged came back "+0.00029, t=+1.55" off exactly TWO
     changed games in 7,000, because the paired t is taken over SEED MEANS and five of seven seeds
     were bit-for-bit tied, which collapses the between-seed sd and manufactures a small se.

     The floor is the same one dead_rung uses in the depth matrix (3*step/n by the rule of three:
     with k differing games in n, the rate of a differing game is bounded by ~3/n, and one differing
     game moves the mean by a whole step). It was learned on burn, where nine rungs were condemned
     reporting "improvement +0.0000, se 0.0000" -- zero observed variance is not zero uncertainty.

PERFORMANCE is reported as core-seconds summed over chunks. That is only comparable because every
arm runs INTERLEAVED IN ONE POOLED BATCH, so contention is shared; wall clock on a loaded box, or
arms run in separate batches, would not be.
"""
import argparse
import collections
import math
import re


def load(path):
    """-> {(arm, seed): [(games, avg, digest, ms), ...]} over chunks."""
    pat = re.compile(r"^(\S+?)_s(\d+)_off(\d+): played=(\d+) avg=([\d.]+) digest=(\w+) ms=([\d.]+)")
    out = collections.defaultdict(list)
    for line in open(path):
        m = pat.match(line.strip())
        if m:
            out[(m.group(1), int(m.group(2)))].append(
                (int(m.group(4)), float(m.group(5)), m.group(6), float(m.group(7))))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("baseline")
    ap.add_argument("--step", type=float, default=1.0,
                    help="smallest change one game can make to the metric (1 whole turn)")
    args = ap.parse_args()

    chunks = load(args.log)
    arms = sorted({a for a, _ in chunks})
    seeds = sorted({s for _, s in chunks})
    if args.baseline not in arms:
        raise SystemExit("baseline %r not among %s" % (args.baseline, arms))

    # fold chunks -> one cell per (arm, seed)
    cell = {}
    for (a, s), rows in chunks.items():
        n = sum(r[0] for r in rows)
        cell[(a, s)] = (n, sum(r[0] * r[1] for r in rows) / n, sum(r[3] for r in rows))

    # only seeds every arm actually holds -- paired or nothing
    ok = [s for s in seeds if all((a, s) in cell for a in arms)]
    if len(ok) < len(seeds):
        print("NOTE dropping %d seed(s) not present in every arm" % (len(seeds) - len(ok)))

    n_per_arm = sum(cell[(args.baseline, s)][0] for s in ok)
    floor = 3.0 * args.step / n_per_arm

    print("\narms %s   paired seeds %d   games/arm %d" % (arms, len(ok), n_per_arm))
    print("resolution floor 3*step/n = %.5f turns -- a |delta| under this is NO MEASUREMENT\n" % floor)
    bavg = sum(cell[(args.baseline, s)][0] * cell[(args.baseline, s)][1] for s in ok) / n_per_arm
    bcost = sum(cell[(args.baseline, s)][2] for s in ok) / 1000.0

    print("%-8s %10s %11s %9s %8s %10s %8s %8s %7s  %s"
          % ("arm", "avg", "delta", "se", "t", "core-s", "cost-x", "cost-se", "cost-t", "verdict"))
    print("%-8s %10.5f %11s %9s %8s %10.0f %8s %8s %7s  %s"
          % (args.baseline + "*", bavg, "-", "-", "-", bcost, "-", "-", "-", "(baseline)"))
    for a in arms:
        if a == args.baseline:
            continue
        n = sum(cell[(a, s)][0] for s in ok)
        avg = sum(cell[(a, s)][0] * cell[(a, s)][1] for s in ok) / n
        cost = sum(cell[(a, s)][2] for s in ok) / 1000.0
        diffs = [cell[(a, s)][1] - cell[(args.baseline, s)][1] for s in ok]
        mean = sum(diffs) / len(diffs)
        sd = math.sqrt(sum((x - mean) ** 2 for x in diffs) / (len(diffs) - 1)) if len(diffs) > 1 else 0.0
        se = sd / math.sqrt(len(diffs)) if sd else 0.0
        t = mean / se if se else float("nan")
        # COST, PAIRED PER SEED -- the leaf is a SPEED lever, not a quality lever (user, 2026-08-16:
        # "It avoids escalation if the win is found and ... avoids the heuristic rollouts at depths
        # other than the chosen one ... It is not expected to be a quality lever, but there should be
        # some speed benefit"). Reporting cost as a bare ratio with no uncertainty is how a real
        # speedup gets discarded as noise: on Creature Giving a model was REJECTED that later proved
        # 0-quality-difference and 30%+ faster. So cost gets the same paired treatment as quality.
        cr = [cell[(a, s)][2] / cell[(args.baseline, s)][2] for s in ok]
        cm = sum(cr) / len(cr)
        csd = math.sqrt(sum((x - cm) ** 2 for x in cr) / (len(cr) - 1)) if len(cr) > 1 else 0.0
        cse = csd / math.sqrt(len(cr)) if csd else 0.0
        ct = (cm - 1.0) / cse if cse else float("nan")
        if abs(mean) < floor:
            qv = "quality UNRESOLVED"
        elif abs(t) >= 2.0:
            qv = "quality %s" % ("BETTER" if mean < 0 else "WORSE")
        else:
            qv = "quality flat"
        if cse and abs(ct) >= 2.0:
            sv = "speed %s %.0f%%" % ("FASTER by" if cm < 1 else "SLOWER by", abs(1 - cm) * 100)
        else:
            sv = "speed inconclusive"
        print("%-8s %10.5f %+11.5f %9.5f %+8.2f %10.0f %8.3fx %8.3f %+7.2f  %s; %s"
              % (a, avg, mean, se, t, cost, cm, cse, ct, qv, sv))
    print("\n[negative delta = better; metric is avg turn-to-win, unwon scored max_turns+1]")
    print("[core-s comparable ONLY because all arms ran interleaved in one pooled batch]")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Paired multi-arm report for a value-leaf regeneration batch log.

THE metric is the loss-penalised average win turn (unwon scored max_turns+1), which is exactly what
the batch line's `avg=` already is -- so negative delta = the arm is BETTER. Pairing is per seed:
every arm runs identical games, so the per-seed difference cancels game-to-game variance and the
spread of those differences is the honest error bar.

Also asserts the seed-tiling invariant (A/B rule 7): per-game identity is base_seed + game_index, so
bases spaced closer than games-per-job make jobs REPLAY games. A violating run reports enormous
significance off a handful of distinct games, and the tell is near-zero variance across seeds.

  usage: vlq_ab_report.py <batch.log> [baseline-arm] [--verdict <arm>]

--verdict <arm> [--tol T] adds one machine-readable line for that arm and sets the exit status:
0 = ADOPT, 2 = do not. valueleaf.sh's adoption step (phase E.5) reads it. The rule is the USER's
(2026-10-11): "We should adopt automatically if the leaf is better", "Is it better is primarily a
performance metric, but we may need to wait for a user response if the quality is notably worse",
"that just means we do not adopt and continue with any other work that remains":

  ADOPT iff  the arm is CHEAPER (work-units ratio < 1 over the same games the quality read uses;
             core-s only when the log carries no units=, since wall-clock moves with host contention)
        and  its quality is NOT NOTABLY WORSE: not (mean delta > T and paired t > +2), T = 0.002 turns
             by default -- the trust tolerance, which prices the same quality-for-cost trade;
        and  the seed tiling is sound and the arm is not byte-identical to the baseline.
  Anything else -> do not adopt; the reason names a TRADE (dearer but better) as the user's call.
"""
import math
import re
import sys
from collections import defaultdict

# Job names may carry a deck prefix ("mirrorwing_dragon-live_s601000") or not ("live_s601000");
# the arm is whatever sits between the optional "<deck>-" prefix and the "_s<seed>" suffix.
LINE = re.compile(r"(?:[\w]+-)?(\w+?)_s(\d+): played=(\d+) avg=([\d.]+) digest=(\w+)(?: ms=(\d+))?(?: units=(\d+))?")


def main(path, baseline=None, verdict=None, tol=0.002):
    arms = defaultdict(dict)
    for ln in open(path):
        m = LINE.match(ln.strip())
        if m:
            arms[m.group(1)][int(m.group(2))] = (int(m.group(3)), float(m.group(4)), m.group(5),
                                                 int(m.group(6) or 0), int(m.group(7) or 0))

    if not arms:
        print(f"no job lines parsed from {path}")
        return 1
    names = sorted(arms)
    if baseline is None:
        baseline = "live" if "live" in arms else names[0]
    if baseline not in arms:
        print(f"baseline arm {baseline!r} not in {names}")
        return 1

    seeds = sorted(set.intersection(*(set(arms[a]) for a in names)))
    if not seeds:
        print(f"no seeds common to all arms {names}")
        return 1

    ids = set()
    for s in seeds:
        ids.update(range(s, s + arms[baseline][s][0]))
    n = sum(arms[baseline][s][0] for s in seeds)
    ok = len(ids) == n
    print(f"arms {names}   paired seeds {len(seeds)}   games/arm {n}   distinct ids {len(ids)}"
          f"   {'OK' if ok else '!! SEED OVERLAP -- result is NOT trustworthy'}")

    def wavg(a):
        return sum(arms[a][s][1] * arms[a][s][0] for s in seeds) / n

    # COST comes with the quality read for free: every job line carries ms= (that job's core-time),
    # so per-arm total core-time and its ratio to baseline report the performance of every arm from
    # the same games the quality verdict used. An arm whose delta is neutral but whose cost is 2x is
    # a real finding (trust/hybrid moves are cost levers first) -- and it was invisible before this.
    def cost_s(a):
        return sum(arms[a][s][3] for s in seeds) / 1000.0

    # THE COST THAT DECIDES IS WORK UNITS, NOT CORE TIME (USER 2026-10-14: "there may be contention, so
    # please do not make decisions based on wall-clock"). ms= is wall-clock on a shared host, so two arms of
    # one batch can read differently only because they ran at different moments; units= is the engine's
    # deterministic work meter. core-s stays as an informative column. A log with no units= on some line
    # falls back to core-s, and the verdict says so.
    def units(a):
        return sum(arms[a][s][4] for s in seeds)
    have_units = all(arms[a][s][4] > 0 for a in names for s in seeds)

    base_avg = wavg(baseline)
    base_cost = cost_s(baseline)
    base_units = units(baseline)
    print(f"\n{'arm':<12} {'avg':<10} {'delta':<10} {'paired t':<10} {'better/worse/tied':<20} "
          f"{'same-digest':<12} {'core-s':<9} {'core vs-base':<13} units vs-base")
    for a in names:
        avg = wavg(a)
        if a == baseline:
            print(f"{a+' (base)':<12} {avg:<10.5f} {'-':<10} {'-':<10} {'-':<20} {'-':<12} "
                  f"{cost_s(a):<9.0f} {'-':<13} -")
            continue
        diffs = [arms[a][s][1] - arms[baseline][s][1] for s in seeds]
        mean = sum(diffs) / len(diffs)
        sd = math.sqrt(sum((x - mean) ** 2 for x in diffs) / (len(diffs) - 1)) if len(diffs) > 1 else 0.0
        se = sd / math.sqrt(len(diffs)) if sd else 0.0
        t = f"{mean/se:+.2f}" if se else "n/a"
        better = sum(1 for x in diffs if x < -1e-9)
        worse = sum(1 for x in diffs if x > 1e-9)
        same = sum(1 for s in seeds if arms[a][s][2] == arms[baseline][s][2])
        note = ""
        if same == len(seeds):
            note = "  <- BYTE-IDENTICAL to baseline (this arm is not engaging)"
        if sd < 1e-12 and same != len(seeds):
            note = "  <- ZERO VARIANCE, suspect replayed games"
        ratio = (f"{cost_s(a)/base_cost:.2f}x" if base_cost > 0 else "n/a")
        uratio = (f"{units(a)/base_units:.3f}x" if have_units and base_units > 0 else "n/a")
        print(f"{a:<12} {avg:<10.5f} {avg-base_avg:<+10.5f} {t:<10} "
              f"{f'{better}/{worse}/{len(seeds)-better-worse}':<20} {f'{same}/{len(seeds)}':<12} "
              f"{cost_s(a):<9.0f} {ratio:<13} {uratio}{note}")
    print("\n[negative delta = better; THE metric is avg turn-to-win with unwon scored max_turns+1]")
    if verdict is not None:
        if verdict not in arms or verdict == baseline:
            print(f"VERDICT arm={verdict} better=0 reason=no-such-arm")
            return 2
        diffs = [arms[verdict][s][1] - arms[baseline][s][1] for s in seeds]
        mean = sum(diffs) / len(diffs)
        sd = math.sqrt(sum((x - mean) ** 2 for x in diffs) / (len(diffs) - 1)) if len(diffs) > 1 else 0.0
        se = sd / math.sqrt(len(diffs)) if sd else 0.0
        same = sum(1 for s in seeds if arms[verdict][s][2] == arms[baseline][s][2])
        if have_units and base_units > 0:
            ratio, basis = units(verdict) / base_units, "units"
        else:
            ratio, basis = (cost_s(verdict) / base_cost if base_cost > 0 else float("inf")), "core-s (NO units= in the log)"
        t = mean / se if se else 0.0
        cheaper = ratio < 1.0
        notably_worse = mean > tol and t > 2.0
        if not ok:
            adopt, why = False, "seed-overlap: the A/B is not trustworthy"
        elif same == len(seeds):
            adopt, why = False, "byte-identical to the baseline: the arm is not engaging"
        elif notably_worse:
            adopt, why = False, f"quality NOTABLY WORSE (+{mean:.5f} > {tol}, t {t:+.2f}) -- the user's call"
        elif not cheaper:
            q = "better" if mean < 0 else "no better"
            adopt, why = False, (f"costs MORE ({ratio:.2f}x) with quality {q} ({mean:+.5f}) -- "
                                 + ("a TRADE, the user's call" if mean < 0 else "not an improvement"))
        else:
            adopt, why = True, f"cheaper ({ratio:.2f}x) and quality not notably worse ({mean:+.5f})"
        print(f"VERDICT arm={verdict} adopt={int(adopt)} delta={mean:+.5f} t={t:+.2f} "
              f"cost={ratio:.3f}x cost_basis={basis.split()[0]} seeds={len(seeds)} games={n} seeds_ok={int(ok)} why={why}")
        return 0 if adopt else 2
    return 0


if __name__ == "__main__":
    args = sys.argv[1:]
    verdict, tol = None, 0.002
    for flag in ("--verdict", "--tol"):
        if flag in args:
            i = args.index(flag)
            if flag == "--verdict":
                verdict = args[i + 1]
            else:
                tol = float(args[i + 1])
            del args[i:i + 2]
    sys.exit(main(args[0], args[1] if len(args) > 1 else None, verdict, tol))

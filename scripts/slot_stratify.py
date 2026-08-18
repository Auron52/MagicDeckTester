#!/usr/bin/env python3
"""Stratify a paired slot contrast by a conditioning variable, to expose MIXTURES.

Why this exists (2026-08-19): the overnight campaign reported Ancestral Anger ->
Fortifying Draught as null (-0.0131, t=-1.52) and concluded Draught earns nothing. It was
an average across a SIGN CHANGE -- conditioned on how many Fists of Flame the game cast,
the same games read -0.1336 (t=-8.35, Draught much better) at 0 Fists and +0.0654 at 2.
A per-card effect is only meaningful once you have checked it is not a mixture.

The contrast: games are selected where the arms DREW the substituted slot and NO other
substituted slot, so the pair differs by exactly one card. Conditioning on a variable that
is itself a CAST COUNT is post-treatment, so read strata as descriptive mechanism, not as
a causal decomposition -- the point is to find sign changes, not to attribute them.

Usage:
  python3 scripts/slot_stratify.py <armA> <armB> --slot 41,42,43,44 --other 47,48,49,54,55,59,60
                                   [--by "Fists of Flame"] [--traces DIR] [--games N]
"""
import json, os, sys, argparse, collections, statistics as st
from multiprocessing import Pool

MAXT = 8
ARGS = None


def _win_turn(g):
    t = (g.get("result") or {}).get("turn", -1)
    return MAXT + 1 if (t is None or t < 0 or t > MAXT) else t


def _scan(a):
    arm, gi = a
    p = f"{ARGS.traces}/{arm}_gi{gi}.json"
    if not os.path.exists(p):
        return None
    g = json.load(open(p))
    seen = {c["card"] for c in g.get("openingHand", [])}
    n_by = 0
    for t in g["turns"]:
        for act in t.get("actions", []):
            ty = act.get("type")
            if ty == "DRAW":
                seen.add(act["card"])
            elif ty == "CAST_SPELL" and act.get("cardName") == ARGS.by:
                n_by += 1
    return gi, _win_turn(g), seen, n_by


def _load(arm, n):
    with Pool(ARGS.jobs) as pool:
        return {r[0]: r[1:] for r in pool.imap_unordered(
            _scan, [(arm, g) for g in range(n)], chunksize=200) if r}


def _row(label, d):
    m = st.mean(d)
    se = st.stdev(d) / len(d) ** .5 if len(d) > 1 else 0.0
    t = m / se if se else 0.0
    return f"{label:>10s} {len(d):>8,} {m:>+9.4f} {se:>8.4f} {t:>7.2f}   {'B' if m < 0 else 'A':>6s}"


def main():
    global ARGS
    ap = argparse.ArgumentParser()
    ap.add_argument("armA"); ap.add_argument("armB")
    ap.add_argument("--slot", required=True, help="card numbers of the substituted slot")
    ap.add_argument("--other", default="", help="every OTHER substituted number (excluded)")
    ap.add_argument("--by", default="Fists of Flame", help="cast-count conditioning card")
    ap.add_argument("--traces", default="logs/overnight/traces")
    ap.add_argument("--games", type=int, default=60000)
    ap.add_argument("--jobs", type=int, default=24)
    ap.add_argument("--cap", type=int, default=3, help="top stratum is '>=cap'")
    ARGS = ap.parse_args()
    SLOT = {int(x) for x in ARGS.slot.split(",") if x.strip()}
    OTHER = {int(x) for x in ARGS.other.split(",") if x.strip()}

    A, B = _load(ARGS.armA, ARGS.games), _load(ARGS.armB, ARGS.games)
    strata, alld = collections.defaultdict(list), []
    for gi in A:
        if gi not in B:
            continue
        wa, sa, fa = A[gi]; wb, sb, fb = B[gi]
        both = sa | sb
        if not (both & SLOT) or (both & OTHER):
            continue
        strata[min(max(fa, fb), ARGS.cap)].append(wb - wa)
        alld.append(wb - wa)
    if not alld:
        sys.exit("no games matched the slot filter")

    print(f"{ARGS.armA} vs {ARGS.armB}   slot={sorted(SLOT)}   stratified by casts of {ARGS.by!r}")
    print(f"(delta = {ARGS.armB} - {ARGS.armA}; negative favours {ARGS.armB})")
    print(f"{'casts':>10s} {'games':>8s} {'delta':>9s} {'se':>8s} {'t':>7s}   {'favours':>6s}")
    for k in sorted(strata):
        print(_row(f"{k}" if k < ARGS.cap else f">={k}", strata[k]))
    print(_row("ALL", alld))
    signs = {m > 0 for m in (st.mean(v) for v in strata.values() if len(v) > 30)}
    if len(signs) > 1:
        print("\n*** SIGN CHANGE across strata -- the pooled number is a MIXTURE, do not report it alone.")


if __name__ == "__main__":
    main()

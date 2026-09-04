#!/usr/bin/env python3
"""WHY is Undercellar Myconid worse than Goblin Instigator? Decompose the +0.0455.

The stratification located the loss in the "no magnet cast" games. That is a LABEL, not a mechanism.
This asks what actually happens in those games:

  1. contribution decomposition  -- share x delta per stratum, so we can see how much of the total
                                   each slice really carries (they must sum to the overall delta).
  2. is the stratum ENDOGENOUS?  -- does the Myconid arm cast a magnet LESS OFTEN than the Instigator
                                   arm? If Myconid suppresses magnet casts, the "no magnet" slice is
                                   partly caused by the card and the stratified read is misleading.
  3. unwon rate + mean win turn  -- a +0.41 delta is enormous; if it is really "more games fail to
                                   win at all", that is a different (and more damaging) claim than
                                   "wins a bit later".
  4. deploy turn + board power   -- the curve story: a 2-mana 1/1+1/1 vs a 3-mana 1/2+1/1.

Reads the traces already on disk from scripts/myconid_stratify.py's batch.
"""
import json, os, glob, collections, statistics as st
from multiprocessing import Pool

T = "logs/myconid_strat/traces"
MAGNETS = {"Mirrorwing Dragon", "Zada, Hedron Grinder"}
SLOT = {"Goblin Instigator", "Undercellar Myconid"}


def scan(args):
    arm, gi, mt = args
    p = f"{T}/{arm}_gi{gi}.json"
    if not os.path.exists(p):
        return None
    g = json.load(open(p))
    t = (g.get("result") or {}).get("turn", -1)
    unwon = (t is None or t < 0 or t > mt)
    wt = mt + 1 if unwon else t
    magnet, slot_turn = None, None
    for tn in g["turns"]:
        for a in tn.get("actions", []):
            if a.get("type") != "CAST_SPELL":
                continue
            nm = a.get("cardName")
            if nm in MAGNETS and magnet is None:
                magnet = nm
            if nm in SLOT and slot_turn is None:
                slot_turn = tn["turn"]
    # board power at the END of the game (last recorded board)
    bf = g["turns"][-1]["boardAfter"]["battlefield"] if g["turns"] else []
    power = sum((p_.get("power") or 0) for p_ in bf if not p_.get("isLand"))
    return gi, wt, unwon, magnet, slot_turn, power, len(g["turns"])


def load(arm, gis, mt):
    with Pool(8) as pool:
        return {r[0]: r[1:] for r in pool.imap_unordered(
            scan, [(arm, g, mt) for g in gis], chunksize=200) if r}


def mean(x):
    return st.mean(x) if x else float("nan")


for tag, mt in (("l20", 8), ("l30", 12)):
    gis = sorted(int(f.split("_gi")[1][:-5]) for f in glob.glob(f"{T}/base_{tag}_gi*.json"))
    if not gis:
        continue
    A = load(f"base_{tag}", gis, mt)       # Goblin Instigator
    B = load(f"myconid_{tag}", gis, mt)    # Undercellar Myconid
    both = [gi for gi in A if gi in B]
    n = len(both)
    print(f"\n{'='*86}\n=== life {tag[1:]}  max_turns {mt}   {n:,} paired games "
          f"  (delta = myconid - base; + = Myconid WORSE)\n{'='*86}")

    # --- 2. endogeneity: magnet-cast rate per ARM -------------------------------------------
    ma = sum(1 for gi in both if A[gi][2]) / n
    mb = sum(1 for gi in both if B[gi][2]) / n
    print(f"\n  magnet cast rate:  Instigator arm {ma:.1%}   Myconid arm {mb:.1%}"
          f"   (diff {mb-ma:+.2%})")

    # --- 1. contribution decomposition ------------------------------------------------------
    strata = collections.defaultdict(list)
    for gi in both:
        strata[A[gi][2] or "none"].append(B[gi][0] - A[gi][0])
    tot = st.mean([d for v in strata.values() for d in v])
    print(f"\n  {'stratum':>22s} {'share':>7s} {'delta':>9s} {'contribution':>13s}")
    for k in ("Mirrorwing Dragon", "Zada, Hedron Grinder", "none"):
        d = strata.get(k, [])
        if not d:
            continue
        sh, m = len(d) / n, st.mean(d)
        print(f"  {str(k):>22s} {sh:>6.1%} {m:>+9.4f} {sh*m:>+13.4f}")
    print(f"  {'TOTAL':>22s} {1.0:>6.1%} {tot:>+9.4f} {tot:>+13.4f}")

    # --- 3./4. inside the no-magnet slice ---------------------------------------------------
    nm = [gi for gi in both if not A[gi][2]]
    print(f"\n  INSIDE the no-magnet slice ({len(nm):,} games):")
    print(f"    {'':<14s} {'unwon':>8s} {'mean wt':>9s} {'slot cast turn':>15s} {'end power':>10s}")
    for lab, S in (("Instigator", A), ("Myconid", B)):
        unwon = sum(1 for gi in nm if S[gi][1]) / len(nm)
        wt = mean([S[gi][0] for gi in nm])
        stn = mean([S[gi][3] for gi in nm if S[gi][3] is not None])
        pw = mean([S[gi][4] for gi in nm])
        print(f"    {lab:<14s} {unwon:>7.1%} {wt:>9.3f} {stn:>15.2f} {pw:>10.2f}")

    # ...against the games that DID cast a magnet, for contrast
    mg = [gi for gi in both if A[gi][2]]
    print(f"\n  for contrast, magnet games ({len(mg):,}):")
    print(f"    {'':<14s} {'unwon':>8s} {'mean wt':>9s} {'slot cast turn':>15s} {'end power':>10s}")
    for lab, S in (("Instigator", A), ("Myconid", B)):
        unwon = sum(1 for gi in mg if S[gi][1]) / len(mg)
        wt = mean([S[gi][0] for gi in mg])
        stn = mean([S[gi][3] for gi in mg if S[gi][3] is not None])
        pw = mean([S[gi][4] for gi in mg])
        print(f"    {lab:<14s} {unwon:>7.1%} {wt:>9.3f} {stn:>15.2f} {pw:>10.2f}")

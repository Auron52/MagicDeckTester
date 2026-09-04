#!/usr/bin/env python3
"""Stratify the Goblin Instigator -> Undercellar Myconid swap.

The unstratified swap measures ~+0.03 (Myconid marginally WORSE). Per
.claude memory `never-report-a-null-unstratified`, a near-zero card result is usually a MIXTURE of
two opposite effects, and the user's card-level reasoning names both sides here (2026-08-26):

  Myconid BETTER : "when you have Mirrorwing rather than Zada" (5 mana vs 4 -- the Myconid ramps)
                   "or insufficient land in hand"              (it is a mana source)
  Myconid WORSE  : "where our manabase bites and we have a tapped land on 2"
                   (Myconid is a THREE-drop; Instigator is a two-drop that still deploys)

So condition on exactly those. Two stratifiers, chosen for what they are:

  open_lands    EXOGENOUS. Inherited numbering means both arms share the shuffle, so the opening
                seven is the same cards bar the substituted slot -- the land count is a property of
                the DEAL, not of play, and cannot be moved by the card under test.
  magnet_cast   ENDOGENOUS, read from the BASE arm only. Which magnet a game casts depends on how it
                plays, so it is a weaker instrument; reading it from one fixed arm keeps the stratum
                definition from being a function of the card under test. Read as descriptive.

delta = myconid - base, in the loss-penalized metric (unwon = max_turns+1). NEGATIVE => Myconid faster.
"""
import json, os, sys, glob, collections, statistics as st
from multiprocessing import Pool

T = "logs/myconid_strat/traces"
LANDS = {"Forest", "Game Trail", "Sandstone Needle", "Gruul Turf", "Mountain", "Rootbound Crag"}
# Lands that CANNOT produce mana the turn they land (Karoo / depletion-tapped). "the manabase bites
# and we have a tapped land on 2" is the user's stated Instigator-favouring case.
ALWAYS_TAPPED = {"Sandstone Needle", "Gruul Turf"}
MAGNETS = {"Mirrorwing Dragon", "Zada, Hedron Grinder"}


def scan(args):
    arm, gi, mt = args
    p = f"{T}/{arm}_gi{gi}.json"
    if not os.path.exists(p):
        return None
    g = json.load(open(p))
    t = (g.get("result") or {}).get("turn", -1)
    wt = mt + 1 if (t is None or t < 0 or t > mt) else t

    open_lands = sum(1 for c in g.get("openingHand", []) if c["cardName"] in LANDS)
    magnet, magnet_turn = "none", None
    t2_tapped_land = False
    for tn in g["turns"]:
        for a in tn.get("actions", []):
            nm, ty = a.get("cardName"), a.get("type")
            if ty == "CAST_SPELL" and nm in MAGNETS and magnet == "none":
                magnet, magnet_turn = nm, tn["turn"]
            if ty == "PLAY_LAND" and tn["turn"] == 2 and nm in ALWAYS_TAPPED:
                t2_tapped_land = True
    return gi, wt, open_lands, magnet, magnet_turn, t2_tapped_land


def load(arm, gis, mt):
    with Pool(24) as pool:
        return {r[0]: r[1:] for r in pool.imap_unordered(
            scan, [(arm, g, mt) for g in gis], chunksize=200) if r}


def report(title, strata, order=None):
    print(f"\n  {title}")
    print(f"    {'stratum':>26s} {'games':>7s} {'share':>7s} {'delta':>9s} {'se':>8s} {'t':>7s}   favours")
    keys = order or sorted(strata)
    tot = [d for k in keys for d in strata.get(k, [])]
    for k in keys:
        d = strata.get(k, [])
        if not d:
            continue
        m = st.mean(d)
        se = st.stdev(d) / len(d) ** .5 if len(d) > 1 else 0.0
        print(f"    {str(k):>26s} {len(d):>7,} {len(d)/len(tot):>6.1%} {m:>+9.4f} {se:>8.4f} "
              f"{(m/se if se else 0):>7.2f}   {'Myconid' if m < 0 else 'Instigator'}")
    m = st.mean(tot); se = st.stdev(tot) / len(tot) ** .5
    print(f"    {'ALL':>26s} {len(tot):>7,} {1.0:>6.1%} {m:>+9.4f} {se:>8.4f} {m/se:>7.2f}"
          f"   {'Myconid' if m < 0 else 'Instigator'}")


def main():
    for tag, mt in (("l20", 8), ("l30", 12)):
        gis = sorted(int(f.split("_gi")[1][:-5])
                     for f in glob.glob(f"{T}/base_{tag}_gi*.json"))
        if not gis:
            print(f"\n=== {tag}: no traces ===")
            continue
        A = load(f"base_{tag}", gis, mt)      # Goblin Instigator
        B = load(f"myconid_{tag}", gis, mt)   # Undercellar Myconid
        both = [gi for gi in A if gi in B]
        print(f"\n{'='*78}\n=== starting life {tag[1:]}   max_turns {mt}   "
              f"{len(both):,} paired games ===\n{'='*78}")

        by_land, by_magnet, by_tap = (collections.defaultdict(list) for _ in range(3))
        for gi in both:
            wa, la, ma, mta, tap_a = A[gi]
            wb, *_ = B[gi]
            d = wb - wa                                  # negative => Myconid faster
            by_land["0-1" if la <= 1 else ("2" if la == 2 else ("3" if la == 3 else "4+"))].append(d)
            by_magnet[ma].append(d)
            by_tap["tapped land on T2" if tap_a else "no tapped land on T2"].append(d)

        report("by OPENING-HAND LANDS (exogenous -- a property of the deal)",
               by_land, ["0-1", "2", "3", "4+"])
        report("by MAGNET CAST, read from the Instigator arm (endogenous -- descriptive)",
               by_magnet, ["Mirrorwing Dragon", "Zada, Hedron Grinder", "none"])
        report("by TURN-2 TAPPED LAND (Karoo/depletion land played on 2)",
               by_tap, ["tapped land on T2", "no tapped land on T2"])


if __name__ == "__main__":
    main()

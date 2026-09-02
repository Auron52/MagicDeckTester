#!/usr/bin/env python3
"""Group the Libation swap's games by SETUP and count them.

The mechanism report answered "what does the card do differently". This answers "in which
situations", which is a different question and needs a different denominator: strata have to be
computed over ALL 20,000 games, not over the ~5% that diverged. A stratum's mean delta measured
only on divergent games is guaranteed to look large -- the identical 95% are exactly the games
where the swap did nothing, and they belong in the average.

Two tiers, kept apart on purpose (same convention as scripts/myconid_stratify.py):

  EXOGENOUS   properties of the DEAL. Inherited numbering means both arms share one shuffle and one
              mulligan decision, so the kept opening hand is the same cards bar the substituted
              slot. Counting "other pumps" (the swapped slot excluded) gives a stratum label that is
              IDENTICAL in both arms and cannot be moved by the card under test. These deltas are
              honest.

  ENDOGENOUS  the board/hand at the moment the swapped card was actually cast. This is a function of
              how each arm played, so the stratum is not a fixed property of the game; it is read
              from ONE arm at a time and reported as DESCRIPTIVE -- it says what the situations
              looked like, not what the swap was worth in them.

usage: mw_libation_setups.py <anger|oracle>
"""
import json, glob, os, re, sys, collections, statistics as st
from multiprocessing import Pool

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WHICH = sys.argv[1] if len(sys.argv) > 1 else "anger"
CFG = {"anger":  {"tag": "anger",  "cut": "Ancestral Anger"},
       "oracle": {"tag": "oracle", "cut": "Oracle's Restoration"}}[WHICH]
CUT, TAG = CFG["cut"], CFG["tag"]
LIB = "Luxurious Libation"
SEED0, NGAMES = 2600000, 20000
FULL = os.path.join(ROOT, "logs/mw_libation/full")

# The deck's solo-target tricks -- the spells a magnet fans out. This IS the pump suite.
PUMPS = {"Gold Rush", "Fists of Flame", "Fortifying Draught", "Ancestral Anger",
         "Oracle's Restoration", "Luxurious Libation"}
MAGNETS = {"Mirrorwing Dragon", "Zada, Hedron Grinder"}
# Frontline Heroism copies a solo-target trick too (makes a Soldier, then copies onto it), so it is
# a fan-out enabler in its own right -- a trick cast with Heroism out is never a lone pump.
COPIERS = MAGNETS | {"Frontline Heroism"}
LANDS = {"Forest", "Game Trail", "Sandstone Needle", "Gruul Turf", "Mountain", "Rootbound Crag"}


def scan(path):
    g = json.load(open(path))
    num2name = {n: name for name, ns in g["cardNumbering"].items() for n in ns}
    won = g["result"].get("winner") == "player"
    wt = g["result"].get("turn") if won else 9
    open_hand = [c["cardName"] for c in g.get("openingHand", [])]
    casts, board, hand = [], {}, {}
    for t in g["turns"]:
        for a in t["actions"]:
            if a["type"] == "CAST_SPELL":
                casts.append((t["turn"], a["cardName"], a.get("manaPaid", "")))
        b = t.get("boardAfter", {})
        board[t["turn"]] = [c["cardName"] for c in b.get("battlefield", [])]
        hand[t["turn"]] = [num2name.get(n, "?") for n in b.get("hand", [])]
    return {"wt": wt, "open": open_hand, "casts": casts, "board": board, "hand": hand,
            "mulls": len(g.get("mulliganSequence", [])) - 1}


def load(tag):
    files = glob.glob(os.path.join(FULL, tag, "*.json"))
    # <ts>_<ts>_<base_seed>_game_<gi>.json -- in a multi-game run the seed field is the BASE seed
    # and the game index is the suffix, so key on the suffix (keying on the seed gives one bucket).
    keys = [int(os.path.basename(f).rsplit("_", 1)[1].split(".")[0]) for f in files]
    with Pool() as p:
        vals = p.map(scan, files, chunksize=64)
    return dict(zip(keys, vals))


def wins_from(errpath):
    got = {}
    for line in open(errpath):
        m = re.match(r"\[win\] gi=(\d+) wt=(-?\d+)", line)
        if m:
            got[int(m.group(1))] = 9 if int(m.group(2)) < 0 else int(m.group(2))
    return got


B = load("base")
A = load(TAG)
print(f"loaded base={len(B)} {TAG}={len(A)} games")

# The screen is the measurement of record; this logging run has to reproduce it or the strata below
# describe some other pair of runs. Checked, not assumed.
scr = json.load(open(os.path.join(ROOT, f"logs/mw_libation/{TAG}_div.json")))
mm = [g for g in B if str(g) in scr["base"] and B[g]["wt"] != scr["base"][str(g)]]
mm += [g for g in A if str(g) in scr["arm"] and A[g]["wt"] != scr["arm"][str(g)]]
print(f"win turns vs the screen: {'MATCH' if not mm else f'{len(mm)} MISMATCH -- {mm[:10]}'}")

gis = sorted(set(B) & set(A))
d = {g: A[g]["wt"] - B[g]["wt"] for g in gis}
print(f"n={len(gis)}  mean delta {st.mean(d.values()):+.4f}  "
      f"(divergent {sum(1 for g in gis if d[g]):,})\n")


def table(title, label_of, note=""):
    """One stratum table. `label_of(gi) -> label or None (drop)`."""
    buckets = collections.defaultdict(list)
    for g in gis:
        lab = label_of(g)
        if lab is not None:
            buckets[lab].append(d[g])
    print(f"--- {title} ---")
    if note:
        print(f"    {note}")
    print(f"    {'setup':<44}{'games':>7}{'%':>7}{'diverge':>9}{'mean delta':>12}{'se':>8}"
          f"{'net turns':>11}")
    tot = sum(len(v) for v in buckets.values())
    for lab in sorted(buckets, key=lambda k: (isinstance(k, str), k)):
        v = buckets[lab]
        m = st.mean(v)
        se = (st.pstdev(v) / len(v) ** 0.5) if len(v) > 1 else float("nan")
        print(f"    {str(lab):<44}{len(v):>7}{100*len(v)/tot:>6.1f}%"
              f"{sum(1 for x in v if x):>9}{m:>+12.4f}{se:>8.4f}{sum(v):>+11}")
    print()


# ---------------------------------------------------------------- EXOGENOUS (all 20,000 games)
def open_pumps(g):
    """Other pumps in the kept opening hand, the swapped slot excluded so the label is identical in
    both arms. Read from base; the arm's hand differs only at that slot."""
    h = B[g]["open"]
    n = sum(1 for c in h if c in PUMPS)
    if CUT in h and (CUT in A[g]["open"] or LIB in A[g]["open"]):
        pass  # the swapped copy may or may not be the one in hand; handled below
    # The arm's hand is base's with at most one CUT swapped for LIB, so:
    #   other pumps = (pumps in base's hand) - (1 if the swapped slot is in hand)
    swapped_in_hand = LIB in A[g]["open"]
    return n - (1 if swapped_in_hand else 0), swapped_in_hand


table("EXOGENOUS: other pump spells in the kept opening hand",
      lambda g: f"{open_pumps(g)[0]} other pumps"
                + ("  [+ the swapped slot]" if open_pumps(g)[1] else ""),
      "identical label in both arms by construction; delta over ALL games in the stratum")

table("EXOGENOUS: is the swapped card itself in the opening hand?",
      lambda g: "swapped card in opening hand" if open_pumps(g)[1] else "drawn later or never")

table("EXOGENOUS: magnet (Mirrorwing / Zada) in the kept opening hand",
      lambda g: f"{sum(1 for c in B[g]['open'] if c in MAGNETS)} magnets in hand")

table("EXOGENOUS: lands in the kept opening hand",
      lambda g: f"{sum(1 for c in B[g]['open'] if c in LANDS)} lands")

table("EXOGENOUS: mulligans taken",
      lambda g: f"mull {B[g]['mulls']}")


# ------------------------------------------------------- ENDOGENOUS (descriptive, arm-read)
def cast_turn(rec, name):
    return next((t for (t, nm, _) in rec["casts"] if nm == name), None)


def at_cast(rec, name):
    """Board/hand state as the swapped card is cast: read the END of the PREVIOUS turn, so the
    chain that same turn has not yet moved the numbers."""
    t = cast_turn(rec, name)
    if t is None:
        return None
    prev = t - 1
    bd = rec["board"].get(prev, [])
    hd = rec["hand"].get(prev, [])
    same_turn = [nm for (tn, nm, _) in rec["casts"] if tn == t]
    i = same_turn.index(name)
    return {
        "turn": t,
        "creatures": sum(1 for c in bd if c not in LANDS),
        "copier": any(c in COPIERS for c in bd),
        "magnet": any(c in MAGNETS for c in bd),
        # other pumps available for this turn = still in hand at end of last turn, plus any already
        # cast this turn before it, minus itself
        "other_pumps": sum(1 for c in hd if c in PUMPS and c != name)
                       + sum(1 for c in same_turn[:i] if c in PUMPS),
        "after": len(same_turn) - i - 1,
    }


# ------------------------------------------------------------- MECHANISM CLASSES, CORRECTED
# Supersedes the divergent-only pass in mw_libation_classify.py, which decided "did this card ever
# reach hand" from the log's DRAW actions. Those record the DRAW STEP ONLY -- a card put in hand by
# a cantrip rider (which is most of them in this deck, since the magnet copies the draw) leaves no
# DRAW action. That undercounted "seen" and inflated the SEARCH_ONLY class ~40x. The hand snapshot
# in boardAfter is authoritative, so everything here reads that instead.
def held(rec, name):
    return (name in rec["open"] or any(name in h for h in rec["hand"].values())
            or any(nm == name for (_, nm, _) in rec["casts"]))


def mech(g):
    a, b = A[g], B[g]
    lib_held, lib_cast = held(a, LIB), any(nm == LIB for (_, nm, _) in a["casts"])
    nb = sum(1 for (_, nm, _) in b["casts"] if nm == CUT)
    na = sum(1 for (_, nm, _) in a["casts"] if nm == CUT)
    # The swapped SLOT is one specific library position. The arm holding Libation is exactly the
    # event of base holding the extra CUT copy, so `lib_held` alone decides whether the slot ever
    # surfaced -- testing `held(b, CUT)` as well would be wrong, since base holds one of its OTHER
    # three Angers in most games and that has nothing to do with the swap.
    if not lib_held:
        return "SEARCH_ONLY   the swapped slot never reached hand"
    if lib_cast and nb > na:
        return "BOTH_CAST     each arm cast its own card"
    if not lib_cast and nb > na:
        return "REPLACED_ONLY base cast its card, Libation stranded"
    if lib_cast:
        return "LIBATION_ONLY only the arm's card was cast"
    return "NEITHER_CAST  held, cast by neither"


table("MECHANISM classes over all 20,000 games (hand-snapshot based -- supersedes the "
      "divergent-only pass)", mech)


# ------------------------------------------------------- THE JOINT TAXONOMY (mutually exclusive)
# The two axes that actually separated the strata above, crossed, over every one of the 20,000
# games. Every game lands in exactly one group, so the counts sum to 20,000 and the net turns sum
# to the screen's own aggregate -- the swap is fully accounted for, by situation.
def group(g):
    a = A[g]
    held = LIB in a["open"] or any(LIB in h for h in a["hand"].values()) \
        or any(nm == LIB for (_, nm, _) in a["casts"])
    if not held:
        return "1. never drawn it"
    s = at_cast(a, LIB)
    if s is None:
        return "2. held it, never cast it"
    lone = s["other_pumps"] <= 1
    if not s["copier"]:
        return "3. cast with NO copier, no other pump" if lone else \
               "4. cast with NO copier, other pumps up"
    return "5. cast with a copier, no other pump" if lone else \
           "6. cast with a copier, other pumps up"


table("THE SETUPS, crossed and mutually exclusive (arm-read, all 20,000 games)", group,
      "'no other pump' = at most one other trick available that turn; a copier is a magnet or "
      "Frontline Heroism")

for arm_name, rec_of, card in ((TAG, lambda g: A[g], LIB), ("base", lambda g: B[g], CUT)):
    st_ = {g: at_cast(rec_of(g), card) for g in gis}
    cast = {g: v for g, v in st_.items() if v}
    print(f"--- ENDOGENOUS (descriptive): the board when {card} is cast, in the {arm_name} arm ---")
    print(f"    cast in {len(cast):,} of {len(gis):,} games")
    for key, fmt in (("other_pumps", lambda v: f"{min(v,3)}{'+' if v>=3 else ''} other pumps available"),
                     ("copier", lambda v: "a copier in play (magnet or Heroism)" if v else "NO copier in play"),
                     ("magnet", lambda v: "magnet in play" if v else "no magnet in play"),
                     ("creatures", lambda v: f"{min(v,5)}{'+' if v>=5 else ''} creatures in play"),
                     ("turn", lambda v: f"cast on turn {v}")):
        c = collections.Counter(fmt(v[key]) for v in cast.values())
        dd = collections.defaultdict(list)
        for g, v in cast.items():
            dd[fmt(v[key])].append(d[g])
        print(f"    {'':<44}{'games':>7}{'%':>7}{'diverge':>9}{'mean delta':>12}")
        for lab in sorted(c):
            v = dd[lab]
            print(f"    {lab:<44}{c[lab]:>7}{100*c[lab]/len(cast):>6.1f}%"
                  f"{sum(1 for x in v if x):>9}{st.mean(v):>+12.4f}")
        print()

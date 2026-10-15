#!/usr/bin/env python3
"""DOES ADDING DOUBLING SEASON BACK CONVERT THE BEASTMASTER ENABLE TO THE ROBUST ROUTE?

THE USER'S OBSERVATION (2026-10-15): *"Beastmaster may still be okay, but this is only certain if
you can attack with enough creatures to enable it in one attack. That is 7 creatures or 4 with
doubling season."*

That second clause is the lead, and it points somewhere the screen structurally could not look.
Quest counters are PUT ON the enchantment, so Doubling Season's counter half doubles each trigger
(the card's own note: "accrual is 2 at a time, and four attackers under one Season cross the
threshold"). So ONE Doubling Season lowers the one-attack bar from SEVEN attackers to FOUR, and two
Seasons lower it to TWO.

WHY THE SCREEN CUT SEASON TO ZERO ANYWAY, AND WHY THAT IS NOT A CONTRADICTION. Rounds 11-21
measured Doubling Season 4->3 and 4->2 as FREE (-0.0032/+0.0065 and -0.0070/+0.0063) and the
archetype change took it to zero. Every one of those readings was taken in a world where the
MULTI-TURN accrual route is free -- the goldfish attacks with everything every turn at no risk, so
banking two counters a turn for three turns is exactly as good as banking seven at once, and
Season's one job here buys nothing the measurement can see. In a blocking world the two routes are
not remotely equivalent: the one-attack route resolves the anthem before the opponent can tax it,
and the accrual route exposes every banking attack. SO THE SCREEN'S VERDICT ON SEASON IS SOUND FOR
THE QUESTION IT ASKED AND SILENT ON THIS ONE. That is the honest statement, not a defect.

WHAT THIS SCRIPT MEASURES, and what it deliberately does NOT. It measures the MECHANISM -- the rate
at which each list crosses the threshold in a single attack -- not the clock. Win turn is NOT the
question here and should not be read off this script: `mtg` direct is not a paired screen and these
lists differ by a card, so only deck_compare can price the quality cost of adding Season back. The
division of labour is deliberate: this script says whether Season buys the robust route, and a
deck_compare round says what it costs in the goldfish. Both numbers are needed and NEITHER settles
the question alone -- the trade (pay a small measured goldfish cost to buy blocker-robustness the
goldfish cannot score) is a USER decision, not mine.

THE ARITHMETIC IS SEASON-AWARE, which the single-list census did not need to be. With S Doubling
Seasons on the battlefield at declare-attackers, each DECLARED attacker contributes 2^S counters, so

    declared_attackers(T) == (quest[copy][T] - quest[copy][T-1]) / 2^S(T)
    one-attack bar        == ceil(7 / 2^S)      ->  7 at S=0, 4 at S=1, 2 at S=2

S is read off the same board snapshot per turn rather than from the decklist, because a Season in
the deck that never resolved doubles nothing.
"""
import collections
import json
import math
import os
import subprocess
import sys

ROOT = "/workspaces/MagicDeckTester"
PROF = os.path.join(ROOT, "logs/deckcmp/Fungus/aliased/Fungus.profile.json")
BEAST = "Beastmaster Ascension"
SEASON = "Doubling Season"
THRESHOLD = 7
GAMES = int(os.environ.get("CENSUS_GAMES", "800"))
SEED = 1230000            # the SAME block for every list, so the comparison is on the same games
THREADS = int(os.environ.get("CENSUS_THREADS", "4"))

LISTS = [
    ("Season 0 (THE RECOMMENDATION)", "logs/deckcmp/Fungus/r_mycon1_S/Fungus.txt"),
    ("Season 1 (-1 Thallid)",         "logs/fungus_screen/season1_list/Fungus.txt"),
    ("Season 2 (-1 Thallid -1 Sapherd)", "logs/fungus_screen/season2_list/Fungus.txt"),
    # --- the fix the DATA points at, after the Season lever was refuted twice over -------------
    # Turn-4 enables separate almost totally on ONE card: Saproling Burst is out for 84.3% of the
    # robust (one-attack) enables and 2.8% of the exposed ones, while Concordant Crossroads is out
    # for 86.1% of the EXPOSED ones and 24.3% of the robust. The mechanism is not subtle -- haste is
    # what lets this deck make the small early attacks it otherwise could not (every body it makes
    # arrives summoning-sick), so Crossroads is HOW the free banking attacks happen. These three
    # arms cut it and spend the slots on bodies, with a lands-only arm to separate "cutting
    # Crossroads" from "the bodies bought with it".
    ("Crossroads 3->1, +bodies",  "logs/fungus_screen/crossroads1_bodies_list/Fungus.txt"),
    ("Crossroads 3->0, +bodies",  "logs/fungus_screen/crossroads0_bodies_list/Fungus.txt"),
    ("Crossroads 3->1, +Forest (isolation control)",
     "logs/fungus_screen/crossroads1_forest_list/Fungus.txt"),
]


def run(deck, logdir):
    os.makedirs(logdir, exist_ok=True)
    for f in os.listdir(logdir):
        os.remove(os.path.join(logdir, f))
    cmd = [os.path.join(ROOT, "build/PGO/mtg"), os.path.join(ROOT, deck),
           "--games", str(GAMES), "--seed", str(SEED), "--max-turns", "8",
           "--depth", "7", "--budget-ms", "1000", "--ignore-play-profile",
           "--threads", str(THREADS), "--profile", PROF, "--log-dir", logdir]
    subprocess.run(cmd, cwd=ROOT, check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)


def analyse(logdir):
    out = {"n": 0, "won": 0, "wt": [], "beast": 0, "on": 0, "one": 0, "multi": 0,
           "att": collections.Counter(), "bar": collections.Counter(),
           "free": collections.Counter(), "seasons": collections.Counter(),
           "kill_one": 0, "kill_multi": 0}
    for fn in sorted(os.listdir(logdir)):
        if not fn.endswith(".json"):
            continue
        g = json.load(open(os.path.join(logdir, fn)))
        out["n"] += 1
        wt = g["result"]["turn"] if g["result"].get("winner") == "player" else None
        if wt is not None:
            out["won"] += 1
            out["wt"].append(wt)
        quest, nseason, dmg = {}, {}, {}
        for t in g["turns"]:
            tn = t["turn"]
            b = t.get("boardAfter", {})
            cur = quest.setdefault(tn, {})
            for p in b.get("battlefield", []):
                if p.get("cardName") == BEAST:
                    n = next((c["count"] for c in p.get("counters", []) if c["kind"] == "quest"), 0)
                    cur[p["card"]] = max(cur.get(p["card"], 0), n)
            s = sum(1 for p in b.get("battlefield", []) if p.get("cardName") == SEASON)
            nseason[tn] = max(nseason.get(tn, 0), s)
            for a in t.get("actions", []):
                if a.get("type") == "ATTACK":
                    dmg[tn] = max(dmg.get(tn, 0), a.get("damage", 0))
        turns = sorted(quest)
        if any(quest[t] for t in turns):
            out["beast"] += 1
        first, enable = {}, None
        for tn in turns:
            for cn, v in quest[tn].items():
                first.setdefault(cn, (tn, v))
                if v >= THRESHOLD and enable is None:
                    enable = (tn, cn)
        if enable is None:
            continue
        out["on"] += 1
        tn, cn = enable
        ftn, fv = first[cn]
        S = nseason.get(tn, 0)
        out["seasons"][S] += 1
        bar = math.ceil(THRESHOLD / (2 ** S))
        out["bar"][bar] += 1
        one = (ftn == tn and fv >= THRESHOLD)
        prev = [quest[t].get(cn, 0) for t in turns if t < tn]
        delta = quest[tn][cn] - (prev[-1] if prev else 0)
        out["att"][max(1, round(delta / (2 ** S)))] += 1
        if one:
            out["one"] += 1
            if wt == tn:
                out["kill_one"] += 1
        else:
            out["multi"] += 1
            out["free"][len([t for t in turns if t < tn and dmg.get(t)])] += 1
            if wt == tn:
                out["kill_multi"] += 1
    return out


def report(label, o):
    n, on = o["n"], o["on"]
    print(f"\n--- {label} ---")
    print(f"  games {n}   won {o['won']} ({100*o['won']/max(1,n):.1f}%)   "
          f"mean win turn {sum(o['wt'])/max(1,len(o['wt'])):.3f}")
    print(f"  Beastmaster resolved {o['beast']} ({100*o['beast']/max(1,n):.1f}%)   "
          f"anthem ON {on} ({100*on/max(1,n):.1f}% of games, "
          f"{100*on/max(1,o['beast']):.1f}% of those that cast it)")
    bars = ", ".join(f"{k} attackers x{v}" for k, v in sorted(o["bar"].items()))
    print(f"  one-attack bar in force at the enable: {bars or 'n/a'}")
    print(f"  *** ENABLED IN ONE ATTACK: {o['one']}/{on} "
          f"({100*o['one']/max(1,on):.1f}% of enables) ***")
    print(f"      accrued over several attacks: {o['multi']}/{on} "
          f"({100*o['multi']/max(1,on):.1f}%)")
    if o["free"]:
        print("      free banking attacks needed by the exposed route: "
              + ", ".join(f"{k}->{v}" for k, v in sorted(o["free"].items())))
    print(f"  the anthem attack WAS the kill: one-attack {o['kill_one']}, "
          f"accrued {o['kill_multi']}")
    print("  declared attackers at the enable: "
          + ", ".join(f"{k}:{v}" for k, v in sorted(o["att"].items())))


if __name__ == "__main__":
    only = sys.argv[1] if len(sys.argv) > 1 else None
    res = []
    for label, deck in LISTS:
        if only and only not in deck:
            continue
        ld = os.path.join(ROOT, "logs/fungus_screen/route_logs",
                          deck.replace("/", "_").replace(".txt", ""))
        if "--analyse-only" not in sys.argv:
            print(f"running {label} ...", flush=True)
            run(deck, ld)
        res.append((label, analyse(ld)))
    print("\n" + "=" * 78)
    print(f"ONE-ATTACK BEASTMASTER ENABLE vs DOUBLING SEASON COUNT  "
          f"({GAMES} games/list, seed {SEED}, d7/b1000)")
    print("=" * 78)
    for label, o in res:
        report(label, o)
    print("\n  Read the ONE-ATTACK line only. Win turn here is NOT a paired measurement and these")
    print("  lists differ by a card -- the quality cost of adding Season back is round 29's job.")

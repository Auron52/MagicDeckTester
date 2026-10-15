#!/usr/bin/env python3
"""WHICH CARDS BUY THE ONE-ATTACK BEASTMASTER ENABLE? Reads the enable-census logs, adds no games.

beastmaster_enable_census.py answers the user's test -- does the deck cross seven quest counters in
a SINGLE attack, which is the only route that is robust to blocking. This script answers the
follow-up that decides what to DO about it: when the deck takes the robust route, what is on the
battlefield that let it, and when it takes the exposed route, what was missing?

That matters because the fix is NOT "cut Beastmaster". Beastmaster's counters come from DECLARED
attackers, so nothing about blocking stops the anthem switching on -- the question is only whether
the deck can put SEVEN bodies in the red zone at once. Cards that buy that are therefore the cards
to protect, and they are not Beastmaster:

  * SAPROLING BURST -- Fading 7, and "Remove a fade counter: create a Saproling whose P/T equals the
    fade counters remaining". The activation costs NO mana and NO tap, so in ONE turn it can make up
    to seven bodies (6/6, 5/5, 4/4, 3/3, 2/2, 1/1, 0/0). It is the only card in the pool that can go
    from zero to a seven-wide attack by itself.
  * CONCORDANT CROSSROADS -- without haste every one of those bodies is summoning-sick and the
    seven-wide attack is a turn late, by which time the counters are being banked two and three at a
    time instead. Its own card note already says it "COMPOUNDS with Beastmaster Ascension rather
    than merely adding to it".
  * BRIGHTCAP BADGER cuts BOTH ways and is worth watching: it gives every Fungus and Saproling
    "{T}: Add {G}", so a body tapped for mana is a body not declared as an attacker. A deck that
    taps four Saprolings to cast something is a deck that banked four fewer quest counters.

So this prints, for the one-attack enables and the multi-attack enables separately, how often each
of those three was on the battlefield on the enabling turn. If the robust route is Burst+Crossroads,
then the honest reading of the user's concern is "the list is fine but its Burst and Crossroads
counts are load-bearing in a way the screen never tested", which is a different -- and cheaper --
conclusion than "Beastmaster is overcounted".
"""
import collections
import json
import os

ROOT = "/workspaces/MagicDeckTester"
LOGS = os.path.join(ROOT, "logs/fungus_screen/enable_census_logs")
BEAST = "Beastmaster Ascension"
WATCH = ["Saproling Burst", "Concordant Crossroads", "Brightcap Badger",
         "Sporecrown Thallid", "Undercellar Myconid", "Shroofus Sproutsire"]
THRESHOLD = 7


def main():
    buckets = {"one-attack (robust)": collections.Counter(),
               "multi-attack (exposed)": collections.Counter()}
    counts = {"one-attack (robust)": 0, "multi-attack (exposed)": 0}
    widths = {"one-attack (robust)": [], "multi-attack (exposed)": []}
    for fn in sorted(os.listdir(LOGS)):
        if not fn.endswith(".json"):
            continue
        g = json.load(open(os.path.join(LOGS, fn)))
        # rebuild the per-copy quest trace exactly as the main census does
        quest, boards = {}, {}
        for t in g["turns"]:
            tn = t["turn"]
            b = t.get("boardAfter", {})
            cur = quest.setdefault(tn, {})
            for p in b.get("battlefield", []):
                if p.get("cardName") == BEAST:
                    n = next((c["count"] for c in p.get("counters", []) if c["kind"] == "quest"), 0)
                    cur[p["card"]] = max(cur.get(p["card"], 0), n)
            names = collections.Counter(p.get("cardName", "") for p in b.get("battlefield", []))
            prev = boards.get(tn, collections.Counter())
            boards[tn] = names if sum(names.values()) >= sum(prev.values()) else prev
        turns = sorted(quest)
        first, enable = {}, None
        for tn in turns:
            for cn, v in quest[tn].items():
                first.setdefault(cn, (tn, v))
                if v >= THRESHOLD and enable is None:
                    enable = (tn, cn)
        if enable is None:
            continue
        tn, cn = enable
        ftn, fv = first[cn]
        key = ("one-attack (robust)" if ftn == tn and fv >= THRESHOLD
               else "multi-attack (exposed)")
        counts[key] += 1
        bf = boards.get(tn, collections.Counter())
        for w in WATCH:
            if bf.get(w, 0):
                buckets[key][w] += 1
        widths[key].append(sum(v for k, v in bf.items()
                               if k not in ("Forest", "Hickory Woodlot", BEAST,
                                            "Concordant Crossroads", "Saproling Burst",
                                            "Wild Growth")))

    print("\n=== What is on the battlefield on the turn the anthem comes on ===\n")
    print(f"  {'card':<24} {'one-attack (robust)':>22} {'multi-attack (exposed)':>24}")
    for w in WATCH:
        a, b = counts["one-attack (robust)"], counts["multi-attack (exposed)"]
        pa = f"{buckets['one-attack (robust)'][w]:>4} ({100*buckets['one-attack (robust)'][w]/max(1,a):>5.1f}%)"
        pb = f"{buckets['multi-attack (exposed)'][w]:>4} ({100*buckets['multi-attack (exposed)'][w]/max(1,b):>5.1f}%)"
        print(f"  {w:<24} {pa:>22} {pb:>24}")
    for k in buckets:
        ws = widths[k]
        print(f"\n  {k}: n={counts[k]}, mean creature bodies on board "
              f"{sum(ws)/max(1,len(ws)):.2f}")


if __name__ == "__main__":
    main()

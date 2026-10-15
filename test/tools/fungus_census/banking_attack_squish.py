#!/usr/bin/env python3
"""WOULD A REAL OPPONENT HAVE SQUISHED THE ATTACKERS THAT BANKED BEASTMASTER'S COUNTERS?

THE USER'S POSITION, now fully stated (2026-10-15):
  * *"Beastmaster may still be okay, but this is only certain if you can attack with enough
    creatures to enable it in one attack. That is 7 creatures or 4 with doubling season."*
  * *"The problem for beastmaster is if we add counters from free attacks. In real games those
    attacks are often not a good idea because our 1/1 can be squished by a blocker."*
  * *"Once the creatures get +5/+5 it is totally fine."*

That is a complete and correct model, and it localises the whole defect precisely. Quest counters
come from DECLARED attackers, so blocking never stops the anthem switching on; and once it is on,
every body is a 6/6 and a 1/1 or 2/2 blocker is irrelevant. So ALL of the overcount lives in the
attacks made BEFORE the anthem -- and those are, by definition, attacks where the counter total is
still under seven, i.e. attacks made with un-pumped bodies. This deck's un-pumped bodies are 1/1 and
2/2 Saprolings. The goldfish declares them for free; a real opponent eats them.

SO THIS SCRIPT PRICES EXACTLY ONE THING: of the counters this deck banks before the anthem, how many
were bought with an attack that a real opponent could have punished, and how hard?

THE OPPONENT'S BOARD IS A LOGGED FACT, NOT A HYPOTHETICAL. GoldFishRunner::PopulateOpponentSpawns
schedules 10 patterns on game_index % 10 and 8 of 10 put real bodies out; GameEngine.cpp:332 names
each one "<power>/<toughness> Creature", so the exact P/T of every available blocker is recoverable
from the board snapshot. They never block (there is no declare-blockers step at all) but they were
THERE, which is what makes this a measurement rather than a thought experiment.

OUR ATTACKERS' SIZE comes from the attack itself: damage / declared attackers is the mean power that
actually connected, post-lord and post-anthem. For this deck that also estimates toughness, because
nearly every body it makes is SQUARE -- 1/1 and 2/2 Saprolings, 2/2 Sporecrown Thallid, 2/2 Yavimaya
Sapherd, and Saproling Burst's tokens whose P/T are both the fade counters remaining. The two
non-square bodies are Brightcap Badger (3/4) and Undercellar Myconid (1/2), and both are TOUGHER
than they are powerful, so treating toughness as equal to power is the CONSERVATIVE direction: it
over-states how easily our attackers die, so a "would not be squished" verdict is trustworthy and a
"would be squished" verdict is the one carrying the approximation. Stated rather than buried.

THE THREE VERDICTS per banking attack, against the biggest body the opponent actually had:
  FREE KILL  blocker power >= our toughness AND blocker toughness > our power
             -- they eat an attacker and keep the blocker. The attack is simply bad.
  TRADE      blocker power >= our toughness AND blocker toughness <= our power
             -- both die. Still bad for the aggressor: we spent a body to bank ONE counter.
  SAFE       blocker power < our toughness -- it cannot kill our attacker, so the attack is fine
             even against a blocking opponent (it may still absorb the damage, but the counter is
             banked and the body survives, which is all the Beastmaster plan needs).
"""
import collections
import json
import os
import re

ROOT = "/workspaces/MagicDeckTester"
LOGS = os.path.join(ROOT, "logs/fungus_screen/enable_census_logs")
BEAST = "Beastmaster Ascension"
PT = re.compile(r"^(\d+)/(\d+) Creature$")


def opp_bodies(bf):
    out = []
    for p in bf:
        m = PT.match(p.get("cardName", ""))
        if m:
            out.append((int(m.group(1)), int(m.group(2))))
    return out


def main():
    verdict = collections.Counter()
    counters = collections.Counter()
    by_turn = collections.defaultdict(collections.Counter)
    sizes = collections.Counter()
    exposed_games = 0
    games_fully_safe = 0
    for fn in sorted(os.listdir(LOGS)):
        if not fn.endswith(".json"):
            continue
        g = json.load(open(os.path.join(LOGS, fn)))
        quest, dmg, opp = {}, {}, {}
        for t in g["turns"]:
            tn, ph = t["turn"], t.get("phase")
            b = t.get("boardAfter", {})
            cur = quest.setdefault(tn, {})
            for p in b.get("battlefield", []):
                if p.get("cardName") == BEAST:
                    q = next((c["count"] for c in p.get("counters", []) if c["kind"] == "quest"), 0)
                    cur[p["card"]] = max(cur.get(p["card"], 0), q)
            for a in t.get("actions", []):
                if a.get("type") == "ATTACK":
                    dmg[tn] = max(dmg.get(tn, 0), a.get("damage", 0))
            # the blockers available when attackers were declared = the board BEFORE combat
            if ph == "MAIN_1" or tn not in opp:
                opp[tn] = opp_bodies(b.get("opponentBattlefield", []))
        turns = sorted(quest)
        first, enable = {}, None
        for tn in turns:
            for cn, v in quest[tn].items():
                first.setdefault(cn, (tn, v))
                if v >= 7 and enable is None:
                    enable = (tn, cn)
        if enable is None:
            continue
        etn, ecn = enable
        ftn, fv = first[ecn]
        if ftn == etn and fv >= 7:
            continue                       # one-attack enable: no banking attacks at all
        exposed_games += 1
        all_safe = True
        for i, tn in enumerate(turns):
            if tn >= etn or not dmg.get(tn):
                continue
            prev = quest[turns[i - 1]].get(ecn, 0) if i else 0
            banked = quest[tn].get(ecn, 0) - prev
            if banked <= 0:
                continue
            mean_p = dmg[tn] / banked      # banked == declared attackers (0 Doubling Season)
            sizes[round(mean_p)] += 1
            bodies = opp.get(tn, [])
            if not bodies:
                v = "no blocker available"
            else:
                bp = max(p for p, _ in bodies)
                bt = max(t for _, t in bodies)
                if bp < mean_p:
                    v = "SAFE (blocker too small)"
                elif bt > mean_p:
                    v = "FREE KILL for the opponent"
                else:
                    v = "TRADE"
            if v.startswith(("FREE", "TRADE")):
                all_safe = False
            verdict[v] += 1
            counters[v] += banked
            by_turn[tn][v] += 1
        if all_safe:
            games_fully_safe += 1

    tot = sum(verdict.values())
    totc = sum(counters.values())
    print("\n=== Would a real opponent have punished the counter-banking attacks? ===")
    print(f"  (the FINAL list, 800 games, d7/b1000; {exposed_games} games took the accrual route)\n")
    print(f"  banking attacks examined          {tot}")
    print(f"  quest counters they banked        {totc}\n")
    print(f"  {'verdict':<32} {'attacks':>12} {'counters banked':>18}")
    for v in ("FREE KILL for the opponent", "TRADE", "SAFE (blocker too small)",
              "no blocker available"):
        if verdict[v]:
            print(f"  {v:<32} {verdict[v]:>5} ({100*verdict[v]/tot:>5.1f}%) "
                  f"{counters[v]:>8} ({100*counters[v]/totc:>5.1f}%)")
    punish = verdict["FREE KILL for the opponent"] + verdict["TRADE"]
    punishc = counters["FREE KILL for the opponent"] + counters["TRADE"]
    print(f"\n  *** PUNISHABLE (free kill or trade): {punish}/{tot} attacks "
          f"({100*punish/tot:.1f}%), carrying {punishc}/{totc} counters "
          f"({100*punishc/totc:.1f}%) ***")
    print(f"  accrual games where NO banking attack was punishable: {games_fully_safe}"
          f"/{exposed_games} ({100*games_fully_safe/max(1,exposed_games):.1f}%)")
    print("\n  mean power of the banking attackers (un-pumped -- the anthem is off by definition):")
    for k in sorted(sizes):
        print(f"    {k}/{k}-ish : {sizes[k]:>5} attacks ({100*sizes[k]/tot:>5.1f}%)")
    print("\n  by turn:")
    for tn in sorted(by_turn):
        row = by_turn[tn]
        n = sum(row.values())
        p = row["FREE KILL for the opponent"] + row["TRADE"]
        print(f"    T{tn}: {n:>4} banking attacks, {p:>4} punishable ({100*p/n:>5.1f}%)")


if __name__ == "__main__":
    main()

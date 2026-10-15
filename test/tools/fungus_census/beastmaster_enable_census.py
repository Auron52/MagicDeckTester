#!/usr/bin/env python3
"""Does the FINAL Fungus list enable Beastmaster Ascension IN ONE ATTACK?

THE USER'S TEST (2026-10-15): *"Beastmaster may still be okay, but this is only certain if you can
attack with enough creatures to enable it in one attack. That is 7 creatures or 4 with doubling
season."*

The arithmetic is exactly right and the engine agrees with it: quest_counter_per_attacker puts ONE
counter per DECLARED attacker (ApplyAttackQuestCounters, CR 508.2), each trigger is its own event so
a Doubling Season doubles each separately -- "accrual is 2 at a time, and four attackers under one
Season cross the threshold" (the card's own note). THE FINAL LIST RUNS ZERO DOUBLING SEASON, so the
one-attack bar here is the full SEVEN.

WHY ONE-ATTACK IS THE RIGHT TEST, stated precisely. Quest counters land at declare-attackers, BEFORE
blockers and before damage, so the anthem applies to the same combat that filled it. Two cases:
  * ENABLED IN ONE ATTACK -- the counters and the +5/+5 both arrive in a combat that a real opponent
    has not yet had a chance to tax. Blocking cannot stop the anthem from switching on; it can only
    eat attackers. With N attackers and the anthem live every attacker deals AT LEAST 5 (the floor is
    a 0/0 Saproling Burst token at +5/+5), so B blockers leave >= 5*(N-B) damage, and 20 life needs
    only N-B >= 4. That attack is genuinely blocker-robust and the user's instinct is right.
  * ACCRUED OVER SEVERAL ATTACKS -- every earlier attack was a real attack into an untapped board
    that the goldfish got for free and a real opponent would have punished. THAT is the overcount.

So this census does not ask "is Beastmaster good" (round 28 measures that). It asks WHICH OF THE TWO
ROUTES the deck actually takes, which is the thing no delta can tell us.

HOW THE ATTACKER COUNT IS RECOVERED EXACTLY, with no new instrumentation. The per-game JSON log
records every permanent's counters by kind (GameLogger.cpp:768, "quest" among them) in each phase's
boardAfter, keyed by the permanent's stable `card` number. With zero Doubling Season the mapping is
one counter per declared attacker, so for any copy present in two consecutive turns

    declared_attackers(turn T) == quest[copy][T] - quest[copy][T-1]

is an IDENTITY, not an estimate. A copy's FIRST recorded quest value is therefore the size of the
first attack made after it landed -- and if that value is already >= 7, the enable was one-attack.

AND THE BLOCKER STRESS TEST USES THE OPPONENT'S REAL BOARD. `opponentBattlefield` is serialised in
the same snapshot, and the opponent's bodies are not empty: GoldFishRunner::PopulateOpponentSpawns
cycles 10 scheduled patterns on game_index % 10 -- 8 of 10 put creatures out (pattern 2 is six 1/1s
by turn 3, pattern 3 is three 2/2s, pattern 8 is a 6/6 plus a 1/1). They never block because the
engine has no declare-blockers step at all, but they ARE there, so "how many blockers did the
opponent have available on the turn we killed them" is a logged fact rather than a hypothetical.
This prices the exposure against the board the deck actually faced.
"""
import collections
import json
import os
import subprocess
import sys

ROOT = "/workspaces/MagicDeckTester"
DECK = "logs/deckcmp/Fungus/r_mycon1_S/Fungus.txt"      # round 27's winner = THE RECOMMENDATION
PROF = "logs/deckcmp/Fungus/aliased/Fungus.profile.json"  # the screen's apparatus, keep table beside it
LOGS = "logs/fungus_screen/enable_census_logs"
GAMES = int(sys.argv[1]) if len(sys.argv) > 1 else 800
SEED = 1230000            # fresh: disjoint from every GT block and from rounds 23-28
THREADS = 4               # round 28 owns the box; counts are contention-proof, wall is not
BEAST = "Beastmaster Ascension"
THRESHOLD = 7


def run():
    os.makedirs(os.path.join(ROOT, LOGS), exist_ok=True)
    for f in os.listdir(os.path.join(ROOT, LOGS)):
        os.remove(os.path.join(ROOT, LOGS, f))
    cmd = [os.path.join(ROOT, "build/PGO/mtg"), os.path.join(ROOT, DECK),
           "--games", str(GAMES), "--seed", str(SEED), "--max-turns", "8",
           "--depth", "7", "--budget-ms", "1000", "--ignore-play-profile",
           "--threads", str(THREADS), "--profile", os.path.join(ROOT, PROF),
           "--log-dir", os.path.join(ROOT, LOGS)]
    print(" ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=ROOT, check=True,
                   stdout=open(os.path.join(ROOT, "logs/fungus_screen/enable_census.run"), "w"),
                   stderr=subprocess.STDOUT)


def creatures(bf):
    """Opponent spawn bodies. They carry no CardDefinition, so isLand is the only filter needed."""
    return [p for p in bf if not p.get("isLand")]


def analyse():
    rows = []
    for fn in sorted(os.listdir(os.path.join(ROOT, LOGS))):
        if not fn.endswith(".json"):
            continue
        g = json.load(open(os.path.join(ROOT, LOGS, fn)))
        win_turn = g["result"]["turn"] if g["result"].get("winner") == "player" else None
        # per turn: {beast card_num -> quest}, attack damage, opponent bodies
        quest, dmg, oppn, ours = {}, {}, {}, {}
        for t in g["turns"]:
            tn = t["turn"]
            b = t.get("boardAfter", {})
            q = {}
            for p in b.get("battlefield", []):
                if p.get("cardName") == BEAST:
                    n = next((c["count"] for c in p.get("counters", []) if c["kind"] == "quest"), 0)
                    q[p["card"]] = n
            # COMBAT's snapshot is the one taken after declare-attackers, so it holds this turn's
            # counters; later phases of the same turn agree, so max() over phases is safe.
            prev = quest.get(tn, {})
            quest[tn] = {k: max(v, prev.get(k, 0)) for k, v in q.items()} if q or prev else {}
            for k, v in prev.items():
                quest[tn].setdefault(k, v)
            for a in t.get("actions", []):
                if a.get("type") == "ATTACK":
                    dmg[tn] = max(dmg.get(tn, 0), a.get("damage", 0))
            oppn[tn] = max(oppn.get(tn, 0), len(creatures(b.get("opponentBattlefield", []))))
            ours[tn] = max(ours.get(tn, 0), len(creatures(b.get("battlefield", []))))

        turns = sorted(quest)
        had_beast = any(quest[t] for t in turns)
        # per-copy trajectory -> the attack sizes, as an identity (1 counter per declared attacker)
        first_seen, attack_sizes, enable = {}, {}, None
        for i, tn in enumerate(turns):
            for cn, v in quest[tn].items():
                if cn not in first_seen:
                    first_seen[cn] = (tn, v)       # v = size of the first attack after it landed
                else:
                    ptn, _ = first_seen[cn]
                    pv = quest[turns[i - 1]].get(cn)
                    if pv is not None:
                        attack_sizes.setdefault(tn, v - pv)
                if v >= THRESHOLD and enable is None:
                    enable = (tn, cn)
        one_attack = None
        if enable is not None:
            tn, cn = enable
            ftn, fv = first_seen[cn]
            # the copy crossed 7 on the very first attack it witnessed
            one_attack = (ftn == tn and fv >= THRESHOLD)
            prev_t = [t for t in turns if t < tn]
            prev_v = quest[prev_t[-1]].get(cn, 0) if prev_t else 0
            n_att = fv if one_attack else (fv if ftn == tn else v_prev_delta(quest, turns, tn, cn))
        rows.append({
            "game": g["gameNumber"], "win_turn": win_turn, "had_beast": had_beast,
            "anthem_on": enable is not None,
            "one_attack": one_attack,
            "enable_turn": enable[0] if enable else None,
            "enable_attackers": (first_seen[enable[1]][1] if enable and one_attack
                                 else (quest[enable[0]][enable[1]] -
                                       max([quest[t].get(enable[1], 0) for t in turns if t < enable[0]] or [0])
                                       if enable else None)),
            "enable_total": quest[enable[0]][enable[1]] if enable else None,
            "opp_bodies": oppn.get(enable[0], 0) if enable else None,
            "dmg": dmg.get(enable[0]) if enable else None,
            "max_attack": max(attack_sizes.values()) if attack_sizes else None,
            "first_attack": max((v for _, v in first_seen.values()), default=None),
            "our_width": ours.get(enable[0]) if enable else max(ours.values(), default=0),
            "max_width": max(ours.values(), default=0),
            # how many attacks banked counters BEFORE the enabling one -- this IS the size of the
            # overcount: each is an attack a real opponent would have had a chance to punish.
            "free_attacks": (0 if enable is None or one_attack
                             else len([t for t in turns if t < enable[0] and dmg.get(t)])),
            # did the anthem attack actually kill? that is the only attack whose block-resilience
            # decides a game; an anthem that comes on a turn we were winning anyway is free.
            "kill_on_enable": (enable is not None and win_turn is not None
                               and enable[0] == win_turn),
        })
    return rows


def v_prev_delta(quest, turns, tn, cn):
    prev = [quest[t].get(cn, 0) for t in turns if t < tn]
    return quest[tn][cn] - (prev[-1] if prev else 0)


def report(rows):
    n = len(rows)
    beast = [r for r in rows if r["had_beast"]]
    on = [r for r in rows if r["anthem_on"]]
    one = [r for r in on if r["one_attack"]]
    multi = [r for r in on if not r["one_attack"]]
    wins = [r for r in rows if r["win_turn"] is not None]
    print(f"\n=== Beastmaster enable census: the FINAL list, {n} games, d7/b1000, seed {SEED} ===")
    print(f"  Doubling Season in this list: 0  ->  the one-attack bar is {THRESHOLD} attackers\n")
    print(f"  games                                    {n}")
    print(f"  won by turn 8                            {len(wins)} ({100*len(wins)/n:.1f}%)"
          f"   mean win turn {sum(r['win_turn'] for r in wins)/max(1,len(wins)):.3f}")
    print(f"  Beastmaster reached the battlefield      {len(beast)} ({100*len(beast)/n:.1f}%)")
    print(f"  anthem switched ON (>= 7 counters)       {len(on)} ({100*len(on)/n:.1f}% of all games,"
          f" {100*len(on)/max(1,len(beast)):.1f}% of games that cast it)")
    print(f"    ENABLED IN ONE ATTACK                  {len(one)} ({100*len(one)/max(1,len(on)):.1f}% of enables)")
    print(f"    accrued over several attacks           {len(multi)} ({100*len(multi)/max(1,len(on)):.1f}% of enables)")

    def dist(label, vals):
        c = collections.Counter(vals)
        print(f"\n  {label}")
        for k in sorted(x for x in c if x is not None):
            print(f"    {k:>3} : {c[k]:>5}  {100*c[k]/max(1,len(vals)):>5.1f}%")

    dist("attackers in the ENABLING attack", [r["enable_attackers"] for r in on])
    dist("largest single attack (any turn, any game with Beastmaster out)",
         [r["max_attack"] or r["first_attack"] for r in beast])
    dist("turn the anthem came on", [r["enable_turn"] for r in on])
    dist("opponent bodies available to block on the enabling turn", [r["opp_bodies"] for r in on])

    dist("free attacks banked before the enable (0 = one-attack enable)",
         [r["free_attacks"] for r in on])

    # THE BLOCKER STRESS TEST, and it is a GUARANTEED FLOOR rather than an estimate. With the anthem
    # live every attacker deals at least 5 (a 0/0 Saproling Burst token at +5/+5 is the floor), and
    # the worst case is the opponent blocking the B BIGGEST attackers, which still leaves the N-B
    # smallest, i.e. >= 5*(N-B). 20 life needs N-B >= 4; 30 life (2hg) needs N-B >= 6. Real damage is
    # far higher -- the list's attackers include 6/6 and 5/5 Burst tokens and lorded Saprolings -- so
    # every percentage below UNDERSTATES the true resilience. Reported as a floor for that reason.
    kills = [r for r in on if r["kill_on_enable"]]
    for label, pop in (("ALL enables", on), ("enables that were the KILLING attack", kills)):
        if not pop:
            continue
        print(f"\n  BLOCKER STRESS FLOOR -- {label} (n={len(pop)})")
        for life, need in ((20, 4), (30, 6)):
            ok = [r for r in pop if r["enable_attackers"] is not None
                  and r["enable_attackers"] - (r["opp_bodies"] or 0) >= need]
            print(f"    lethal from {life:>2} through EVERY body the opponent actually had: "
                  f"{len(ok):>4}/{len(pop)} ({100*len(ok)/len(pop):>5.1f}%)")
        for b in (1, 2, 3, 4):
            ok = [r for r in pop if r["enable_attackers"] is not None
                  and r["enable_attackers"] - b >= 4]
            print(f"    lethal from 20 through {b} blocker(s):{'':>26}"
                  f"{len(ok):>4}/{len(pop)} ({100*len(ok)/len(pop):>5.1f}%)")
        d = [r["dmg"] for r in pop if r["dmg"]]
        if d:
            d.sort()
            print(f"    damage dealt by that attack: median {d[len(d)//2]}, "
                  f"p10 {d[len(d)//10]}, p90 {d[(9*len(d))//10]}  (lethal needs 20 / 30)")
    print(f"\n  board width when the anthem came on: mean "
          f"{sum(r['our_width'] or 0 for r in on)/max(1,len(on)):.2f} creatures")
    print(f"  games that never turned the anthem on still won: "
          f"{len([r for r in rows if not r['anthem_on'] and r['win_turn']])}"
          f"/{len([r for r in rows if not r['anthem_on']])}")


if __name__ == "__main__":
    if "--analyse-only" not in sys.argv:
        run()
    report(analyse())

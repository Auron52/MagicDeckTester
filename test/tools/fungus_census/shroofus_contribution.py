#!/usr/bin/env python3
"""HOW MUCH OF SHROOFUS'S PRODUCTION EVER ATTACKS? The exposure test for the second suspect card.

Beastmaster and Shroofus are grouped in the user's question but their exposure mechanisms are
OPPOSITE in shape, and that difference decides what to do about each:

  * BEASTMASTER pays out in the SAME combat. Quest counters land at declare-attackers (CR 508.2),
    before blockers and before damage, so the anthem applies to the combat that filled it. Nothing a
    blocker does stops the anthem switching on. Its exposure is only in HOW the counters were banked,
    which beastmaster_enable_census.py measures.
  * SHROOFUS pays out STRICTLY NEXT TURN, and that is structural, not incidental. Its tokens are
    created in the COMBAT-DAMAGE step, so by the card's own note they "enter summoning-sick and
    cannot join the combat that made them -- and a haste grant does NOT change that, because there
    is no second combat step". They also "arrive strictly AFTER Beastmaster Ascension's quest
    counters ... so they bank zero counters toward their own combat."

So Shroofus needs connect(T) -> tokens -> survive to T+1 -> connect AGAIN, and the token COUNT is the
damage dealt, which is precisely the quantity a blocker reduces. That is multiplicative exposure. But
it is also exposure to something far more mundane that the goldfish hides: IF THE GAME ENDS ON TURN
T, the tokens made on turn T never do anything at all. At a mean win turn of 4.76 that is not a
corner case, it is most of the deck's games -- and it is measurable without any blocker model.

HOW THE PRODUCTION IS RECOVERED EXACTLY. A turn emits one board snapshot per phase. MAIN_1's is
BEFORE combat and COMBAT's is AFTER it, and the only card in this list that creates a token during
the combat-damage step is Shroofus. So

    tokens_created_in_combat(T) == creatures(COMBAT, T) - creatures(MAIN_1, T)

is Shroofus's production on that turn, and the tokens are then classified by whether a LATER attack
existed at all. Brightcap Badger's end-step token is excluded by construction: the end step is after
the COMBAT snapshot, so it lands in the NEXT turn's MAIN_1 baseline rather than inside this delta.
"""
import collections
import json
import os

ROOT = "/workspaces/MagicDeckTester"
LOGS = os.path.join(ROOT, "logs/fungus_screen/enable_census_logs")
SHROOF = "Shroofus Sproutsire"


def main():
    games = out_on = 0
    made_total = made_wasted = 0
    per_turn = collections.Counter()
    wasted_turn = collections.Counter()
    games_with_production = games_all_wasted = 0
    for fn in sorted(os.listdir(LOGS)):
        if not fn.endswith(".json"):
            continue
        g = json.load(open(os.path.join(LOGS, fn)))
        games += 1
        wt = g["result"]["turn"] if g["result"].get("winner") == "player" else None
        pre, post, shroof, atk = {}, {}, {}, {}
        for t in g["turns"]:
            tn, ph = t["turn"], t.get("phase")
            b = t.get("boardAfter", {})
            crt = [p for p in b.get("battlefield", []) if not p.get("isLand")]
            n = len(crt)
            if ph == "MAIN_1":
                pre[tn] = n
            elif ph == "COMBAT":
                post[tn] = n
                shroof[tn] = sum(1 for p in crt if p.get("cardName") == SHROOF)
            for a in t.get("actions", []):
                if a.get("type") == "ATTACK":
                    atk[tn] = True
        if any(shroof.values()):
            out_on += 1
        produced = {}
        for tn in sorted(post):
            if tn in pre and shroof.get(tn, 0) and atk.get(tn):
                d = post[tn] - pre[tn]
                if d > 0:
                    produced[tn] = d
        if produced:
            games_with_production += 1
        allw = bool(produced)
        for tn, d in produced.items():
            made_total += d
            per_turn[tn] += d
            # a token made on turn tn can only ever attack on turn tn+1 or later. If the game ended
            # on tn (or no attack happened afterwards), it did nothing.
            later = any(atk.get(t) for t in range(tn + 1, 20))
            if not later:
                made_wasted += d
                wasted_turn[tn] += d
            else:
                allw = False
        if produced and allw:
            games_all_wasted += 1

    print(f"\n=== Shroofus Sproutsire: how much of its production ever attacks? ===")
    print(f"  (the FINAL list, {games} games, d7/b1000; 2 copies in 60)\n")
    print(f"  games with a Shroofus on the battlefield at some combat   {out_on} "
          f"({100*out_on/games:.1f}%)")
    print(f"  games where it actually produced tokens in combat         {games_with_production} "
          f"({100*games_with_production/games:.1f}%)")
    print(f"  tokens created in the combat-damage step, total           {made_total}")
    print(f"  ... of which NO LATER ATTACK EVER HAPPENED                {made_wasted} "
          f"({100*made_wasted/max(1,made_total):.1f}%)")
    print(f"  games where EVERY token it made was wasted                {games_all_wasted}"
          f"/{games_with_production} "
          f"({100*games_all_wasted/max(1,games_with_production):.1f}%)")
    print(f"\n  production by turn (wasted = the game ended before they could attack):")
    for tn in sorted(per_turn):
        print(f"    T{tn}: {per_turn[tn]:>5} tokens, {wasted_turn[tn]:>5} wasted "
              f"({100*wasted_turn[tn]/per_turn[tn]:>5.1f}%)")
    print(f"\n  mean tokens per producing game: "
          f"{made_total/max(1,games_with_production):.2f}")


if __name__ == "__main__":
    main()

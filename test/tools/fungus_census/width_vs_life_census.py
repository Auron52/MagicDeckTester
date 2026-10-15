#!/usr/bin/env python3
"""DOES DOUBLING SEASON BUY WIDTH, AND DOES WIDTH PAY WHEN THE CLOCK IS LONGER?

THE USER'S ARGUMENT (2026-10-15), which is a criticism of my own Season refutation and I think it is
correct:
  * *"I still think Doubling season is hard to fully credit in these arms because it gives you a
    crazy board with Saproling Burst or other effects, but it doesn't have the ability to be useful
    on games that end T4."*
  * *"So, it is generically better against actual decks in which the board size really matters."*
  * *"Hence, I would see if we can keep at least 1 or 2."* / *"Without a big cost to our win turns."*
  * *"I can definitely see how multiples are not ideal, so perhaps 4 is too many."* /
    *"(but 2 multiples would be rare)"*

WHY THE CRITICISM LANDS. My earlier refutation was "Season resolves in time in only 6.1% of games,
and when it IS out the one-attack rate FALLS". Both numbers are true and both are measured against a
clock of 4.76 turns -- a clock the blocker overcount itself shortens, since ~17% of games would see
their anthem arrive a turn later against a real opponent. So "too slow to matter" is circular: it is
measured in the very world whose speed is the thing in question.

AND THE DEEPER POINT IS THE WIDTH ONE. The goldfish scores TOTAL DAMAGE -- it needs 20, from any
shape of board. A real opponent blocking taxes WIDTH specifically: each blocker subtracts one
attacker, so what matters is N - B, not total power. Doubling Season's entire contribution here is
width (it doubles every token through CreateToken, the single chokepoint -- and Saproling Burst's
seven free activations become fourteen bodies). So Season is under-credited for EXACTLY the reason
the free banking attacks are over-credited: the apparatus prices damage, not width. They are two
sides of one modelling gap, which is why the user's intuition tracks it.

WHAT THIS SCRIPT MEASURES. The same per-game logs as the other censuses, but swept over BOTH the
Season count AND the starting life total (MTG_START_LIFE, GameSetup.h:65). Life is the only lever
available that lengthens the clock, and lengthening the clock is precisely the axis the user says
Season needs. Reported per cell:

  * SEASON DEPLOY RATE -- does it resolve at all, and on what turn? If raising life does not fix
    this, the "too slow" objection survives the user's criticism and Season genuinely cannot work.
  * WIDTH at the enable -- declared attackers, which is the quest-counter delta divided by 2^S.
    This is the quantity the user's argument is about.
  * THE BLOCKER STRESS FLOOR, now life-aware. With the anthem live every attacker deals at least 5,
    so punching L damage through B blockers needs N - B >= ceil(L/5): FOUR unblocked attackers at 20
    life, SIX at 30, EIGHT at 40. The bar RISES with the clock, which is the whole reason width
    should matter more in a longer game -- and therefore the cleanest test of the user's claim.

WHAT IT DOES NOT MEASURE: the win-turn cost of adding Season, which is the user's stated budget
(*"without a big cost to our win turns"*). That needs a paired screen and is round 30's job; `mtg`
direct is not paired and these lists differ by cards. Read the WIDTH and DEPLOY columns here only.
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
GAMES = int(os.environ.get("W_GAMES", "400"))
THREADS = int(os.environ.get("W_THREADS", "6"))
SEED = 1250000
LISTS = [
    ("Shroofus 1, Season 0", "logs/fungus_screen/s1_base_list/Fungus.txt"),
    ("Shroofus 1, Season 1", "logs/fungus_screen/s1_season1_list/Fungus.txt"),
    ("Shroofus 1, Season 2", "logs/fungus_screen/s1_season2_list/Fungus.txt"),
    ("Beast 3, Shroof 1, Season 2", "logs/fungus_screen/s1_b3_season2_list/Fungus.txt"),
]
LIVES = [20, 40]


def run(deck, logdir, life):
    os.makedirs(logdir, exist_ok=True)
    for f in os.listdir(logdir):
        os.remove(os.path.join(logdir, f))
    env = dict(os.environ, MTG_START_LIFE=str(life))
    # max-turns must clear the longer clock or wins get scored as no-wins and width is truncated
    mt = "8" if life <= 20 else "16"
    subprocess.run([os.path.join(ROOT, "build/PGO/mtg"), os.path.join(ROOT, deck),
                    "--games", str(GAMES), "--seed", str(SEED), "--max-turns", mt,
                    "--depth", "7", "--budget-ms", "1000", "--ignore-play-profile",
                    "--threads", str(THREADS), "--profile", PROF, "--log-dir", logdir],
                   cwd=ROOT, check=True, env=env,
                   stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)


def analyse(logdir, life):
    need = math.ceil(life / 5)        # unblocked attackers needed with the anthem live
    o = {"n": 0, "wt": [], "deploy": 0, "deploy_turn": collections.Counter(),
         "on": 0, "one": 0, "att": [], "floor": 0, "floor_noblock": 0,
         "maxwidth": [], "need": need, "max_simul": collections.Counter()}
    for fn in sorted(os.listdir(logdir)):
        if not fn.endswith(".json"):
            continue
        g = json.load(open(os.path.join(logdir, fn)))
        o["n"] += 1
        if g["result"].get("winner") == "player":
            o["wt"].append(g["result"]["turn"])
        quest, ns, opp, crt = {}, {}, {}, {}
        for t in g["turns"]:
            tn = t["turn"]
            b = t.get("boardAfter", {})
            cur = quest.setdefault(tn, {})
            for p in b.get("battlefield", []):
                if p.get("cardName") == BEAST:
                    q = next((c["count"] for c in p.get("counters", []) if c["kind"] == "quest"), 0)
                    cur[p["card"]] = max(cur.get(p["card"], 0), q)
            ns[tn] = max(ns.get(tn, 0),
                         sum(1 for p in b.get("battlefield", []) if p.get("cardName") == SEASON))
            opp[tn] = max(opp.get(tn, 0),
                          len([p for p in b.get("opponentBattlefield", []) if not p.get("isLand")]))
            crt[tn] = max(crt.get(tn, 0),
                          len([p for p in b.get("battlefield", []) if not p.get("isLand")]))
        st = [t for t in sorted(ns) if ns[t] > 0]
        if st:
            o["deploy"] += 1
            o["deploy_turn"][st[0]] += 1
        # USER 2026-10-15: *"I can definitely see how multiples are not ideal, so perhaps 4 is too
        # many"* / *"(but 2 multiples would be rare) (with 2)"*. That is a testable claim about this
        # list, not a guess -- the most Seasons ever simultaneously on the battlefield IS the
        # multiples rate, and a redundant doubler is the anti-synergy being worried about.
        o["max_simul"][max(ns.values(), default=0)] += 1
        o["maxwidth"].append(max(crt.values(), default=0))
        turns = sorted(quest)
        first, en = {}, None
        for tn in turns:
            for cn, v in quest[tn].items():
                first.setdefault(cn, (tn, v))
                if v >= 7 and en is None:
                    en = (tn, cn)
        if en is None:
            continue
        o["on"] += 1
        tn, cn = en
        ftn, fv = first[cn]
        S = ns.get(tn, 0)
        if ftn == tn and fv >= 7:
            o["one"] += 1
        prev = [quest[t].get(cn, 0) for t in turns if t < tn]
        delta = quest[tn][cn] - (prev[-1] if prev else 0)
        N = max(1, round(delta / (2 ** S)))
        o["att"].append(N)
        if N - opp.get(tn, 0) >= need:
            o["floor"] += 1
        if N >= need:
            o["floor_noblock"] += 1
    return o


def report(label, life, o):
    n, on = o["n"], o["on"]
    att = o["att"]
    print(f"\n  {label}  @ {life} life")
    print(f"    won {len(o['wt'])}/{n}   mean win turn {sum(o['wt'])/max(1,len(o['wt'])):.2f}"
          f"   mean max board width {sum(o['maxwidth'])/max(1,n):.1f}")
    dt = ", ".join(f"T{k}:{v}" for k, v in sorted(o["deploy_turn"].items())) or "never"
    print(f"    Season resolved in {o['deploy']}/{n} ({100*o['deploy']/n:.1f}%)   first-resolve {dt}")
    ms = o["max_simul"]
    if sum(k for k in ms):
        print(f"    MOST Seasons on the battlefield at once: "
              + ", ".join(f"{k} in {ms[k]} games ({100*ms[k]/n:.1f}%)" for k in sorted(ms)))
    print(f"    anthem ON {on} ({100*on/n:.1f}%)   one-attack {o['one']}/{on} "
          f"({100*o['one']/max(1,on):.1f}%)")
    if att:
        att_s = sorted(att)
        print(f"    declared attackers at the enable: mean {sum(att)/len(att):.2f}, "
              f"median {att_s[len(att_s)//2]}, p90 {att_s[(9*len(att_s))//10]}")
    print(f"    needs {o['need']} UNBLOCKED attackers at this life total:")
    print(f"      wide enough ignoring blockers:        {o['floor_noblock']}/{on} "
          f"({100*o['floor_noblock']/max(1,on):.1f}%)")
    print(f"      *** wide enough THROUGH the bodies the opponent had: {o['floor']}/{on} "
          f"({100*o['floor']/max(1,on):.1f}%) ***")


if __name__ == "__main__":
    only = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("--") else None
    print(f"=== WIDTH vs CLOCK: Doubling Season across starting life ({GAMES} games/cell, "
          f"seed {SEED}, d7/b1000) ===")
    for life in LIVES:
        for label, deck in LISTS:
            if only and only not in deck:
                continue
            ld = os.path.join(ROOT, "logs/fungus_screen/width_logs",
                              f"{os.path.basename(os.path.dirname(deck))}_L{life}")
            if "--analyse-only" not in sys.argv:
                print(f"running {label} @ {life} ...", flush=True)
                run(deck, ld, life)
            report(label, life, analyse(ld, life))
    print("\n  Read DEPLOY RATE and WIDTH only. The win-turn COST of adding Season needs a paired")
    print("  screen (round 30); mtg direct is unpaired and these lists differ by cards.")

#!/usr/bin/env python3
"""HOW OFTEN COULD THE FADE-MENU CDA DEFECT ACTUALLY BITE?

Context. `ReadFadeBoard` priced every permanent through `Permanent::EffectivePower()`, which knows
nothing about a characteristic-defining ability, so The Mycotyrant's `*/*` read as ZERO POWER in the
Saproling Burst activation-count menu. Fixed 2026-10-15; see
docs/design/fade-k-menu-ignores-cda-power.md.

THE PUZZLE THIS CENSUS EXISTS TO SETTLE. Pre-fix and post-fix binaries scored IDENTICALLY on the
`v_rule_myco1` list -- 4.8760 on 2000 std games, to four decimals, i.e. not one game changed. That
is either (a) the defect is almost never ARMED, or (b) the fix is not wired up. Those have opposite
consequences and "the numbers matched" cannot tell them apart -- the repo's own lesson
([[digest-equality-can-mean-broken]]) is that a widening change once emitted ZERO variants and read
as clean. So measure the arming condition directly instead of inferring it.

THE ARMING CONDITION, stated exactly. The defect can only change a decision when ALL of:
  1. The Mycotyrant is ON THE BATTLEFIELD (it is the only card in the 533-card file whose power is a
     subtype-count CDA and that also shares a deck with a fading source), AND
  2. a Saproling Burst is ON THE BATTLEFIELD in the same turn -- its presence implies it still holds
     fade counters, since fading sacrifices it the moment it cannot remove one (CR 702.32), AND
  3. the menu is actually consulted for that Burst, which for the `base` branch means the turn it
     LANDED (`own == 0`).

This census measures 1 and 2 exactly, and 3 as the turn a Burst first appears while a Mycotyrant is
already out. 1-and-2 is the UPPER BOUND on how often the defect could matter; the landing-turn
figure is the tight one for the `base` menu.

WHAT IT CANNOT SEE, said plainly: `fade_counters` lives in a dedicated `Permanent::fade_counters`
int rather than a `Counter{}` entry (a deliberate perf choice recorded on the Saproling Burst card
note), and GameLogger only serialises `p.counters`. So the exact counter total -- hence the exact
argmax -- is not recoverable from a log. Presence is, and presence is what bounds the arming rate.
"""
import collections
import json
import os
import subprocess
import sys

ROOT = "/workspaces/MagicDeckTester"
DECK = "logs/myco_ab/FungusMyco.cod"                       # the round-31 v_rule_myco1 arm
PROF = "logs/deckcmp/Fungus/aliased/Fungus.profile.json"   # the screen's apparatus
LOGS = "logs/myco_ab/arming_logs"
GAMES = int(sys.argv[1]) if len(sys.argv) > 1 else 800
SEED = 1290000            # disjoint from every GT block, rounds 23-31 and the other censuses
THREADS = int(sys.argv[2]) if len(sys.argv) > 2 else 32
TYRANT = "The Mycotyrant"
BURST = "Saproling Burst"


def run():
    d = os.path.join(ROOT, LOGS)
    os.makedirs(d, exist_ok=True)
    for f in os.listdir(d):
        os.remove(os.path.join(d, f))
    cmd = [os.path.join(ROOT, "build/Release/mtg"), os.path.join(ROOT, DECK),
           "--cards-json", os.path.join(ROOT, "src/cards/data/cards.json"),
           "--games", str(GAMES), "--seed", str(SEED), "--max-turns", "8",
           "--depth", "7", "--budget-ms", "1000",
           "--threads", str(THREADS), "--profile", os.path.join(ROOT, PROF),
           "--log-dir", d]
    print(" ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=ROOT, check=True,
                   stdout=open(os.path.join(ROOT, "logs/myco_ab/arming.run"), "w"),
                   stderr=subprocess.STDOUT)


def analyse():
    n = 0
    tyrant_games = 0          # Mycotyrant reached the battlefield at all
    both_games = 0            # ...in the same turn as a Burst (the UPPER BOUND)
    landing_games = 0         # ...and a Burst FIRST appeared while it was already out (the base menu)
    first_tyrant_turn = collections.Counter()
    both_turns = collections.Counter()
    for fn in sorted(os.listdir(os.path.join(ROOT, LOGS))):
        if not fn.endswith(".json"):
            continue
        g = json.load(open(os.path.join(ROOT, LOGS, fn)))
        n += 1
        seen_t, seen_both, seen_landing = False, False, False
        burst_seen_before = False
        t_out_before = False
        for t in g.get("turns", []):
            bf = (t.get("boardAfter") or {}).get("battlefield") or []
            names = [p.get("cardName") for p in bf]
            has_t = TYRANT in names
            has_b = BURST in names
            turn_no = t.get("turn")
            if has_t and not seen_t:
                seen_t = True
                first_tyrant_turn[turn_no] += 1
            if has_t and has_b:
                if not seen_both:
                    seen_both = True
                    both_turns[turn_no] += 1
                # a Burst appearing for the first time WHILE the Mycotyrant is already out is the
                # node where the `base` menu (own == 0) is built with the CDA in play
                if not burst_seen_before and t_out_before:
                    seen_landing = True
            burst_seen_before = burst_seen_before or has_b
            t_out_before = t_out_before or has_t
        tyrant_games += seen_t
        both_games += seen_both
        landing_games += seen_landing
    print(f"\n  games                                          {n}")
    print(f"  The Mycotyrant reached the battlefield         {tyrant_games:>5}  "
          f"({tyrant_games / max(1, n):.1%})")
    print(f"  ...with a Saproling Burst out the same turn    {both_games:>5}  "
          f"({both_games / max(1, n):.1%})   <- UPPER BOUND on the defect's reach")
    print(f"  ...and a Burst LANDED while it was out        {landing_games:>5}  "
          f"({landing_games / max(1, n):.1%})   <- the `base` menu's own rate")
    if first_tyrant_turn:
        print("\n  first turn the Mycotyrant was on the battlefield: "
              + ", ".join(f"T{k}:{v}" for k, v in sorted(first_tyrant_turn.items())
                          if k is not None))
    if both_turns:
        print("  first turn both were out together:               "
              + ", ".join(f"T{k}:{v}" for k, v in sorted(both_turns.items())
                          if k is not None))
    print("\n  READ IT THIS WAY: the last line bounds how often the pre-fix menu could have been\n"
          "  built with a Mycotyrant mis-read as 0 power. If it is a few percent, an identical\n"
          "  2000-game average is EXPECTED and the fix is latent rather than unwired -- and the\n"
          "  card being cast rarely is then its own, separate finding about the list.")


if __name__ == "__main__":
    if "--analyse-only" not in sys.argv:
        run()
    analyse()

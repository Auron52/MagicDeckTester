#!/usr/bin/env python3
"""HOW OFTEN DO DOUBLING SEASON AND SAPROLING BURST ACTUALLY MEET, AND HOW BIG IS THE BOARD?

WHY THIS EXISTS. The screen measures AVERAGE WIN TURN, and on that metric a 3rd Doubling Season is a
COST (round 32, on the ruled base: +0.0239 std / +0.0251 2hg). The USER's case for the 3rd copy is
not about speed:

    *"Season is super good in longer games because we can take full advantage of its effect, so that
      is the main reason why 3 is attractive from a player's point of view. However, it is true that
      it doesn't help as much as I would like toward fast wins."*
    *"It's not 4 Saproling Burst, but Saproling Burst + Season."*
    *"Which produces huge boards for the player."*
    *"The real case I'm thinking about is where you are facing a board on the other end."*

THE APPARATUS CANNOT ANSWER THE LAST ONE AND IT IS NOT A WEIGHTING PROBLEM. `src/ai/Combat.cpp` has
NO DECLARE-BLOCKERS STEP: the attack policy is the hard-coded goldfish default and 100% of every
attacker's power reaches the player. Width is therefore worth nothing beyond the damage it deals, so
a board that is "hard to stop" scores exactly as a board that is easy to stop. Raising the opponent's
life to 40 does NOT fix this -- the games get longer, the opponent still never blocks. So no screen
arm, in any format, can price the benefit the user is describing.

WHAT CAN BE MEASURED, AND IS. The user's claim decomposes into two facts that ARE observable:

  1. CO-OCCURRENCE -- how often a Doubling Season and a Saproling Burst are on the battlefield
     together, which is the arming condition for the whole plan. This is the number the 3rd copy is
     bought for, and it is a probability, not a judgement.
  2. BOARD SIZE -- how many bodies the deck actually fields, overall and CONDITIONAL on the two
     having met. That is the "huge boards" claim, stated as a count.

Together they turn an unmeasurable into a priced trade: "a 3rd Season raises the arming rate by X
points and adds Y bodies when it arms, and costs 0.024 turns of speed." The user can then decide,
which is the only honest shape for this answer. Same method as mycotyrant_arming_census.py, which
explained a null the same way.

THE ARITHMETIC THE CENSUS DOES NOT NEED TO MEASURE, because it is exact: Saproling Burst enters with
`fading_counters: 7`, and both ETB sites (EffectHandler.cpp:138, TurnSolver.cpp:40784) route through
`PutFadeCounters`, which applies `DoublerShift`. So under ONE Season a Burst enters with 14 counters
and each activation makes 2 Saprolings -- a ceiling of 28 bodies instead of 7, the first of them a
13/13. Under TWO Seasons, 28 counters. The engine models the combination in full, and generously;
what it cannot model is an opponent who would mind.

WHAT IT CANNOT SEE, said plainly: `fade_counters` is a dedicated `Permanent` int and GameLogger
serialises only `p.counters` (the probe shows `depletion` counters logged and no `fade`), so TOKEN
POWER is not recoverable from a log. Board size here is therefore BODIES, not power -- which
understates the Season board, since Season also makes each Burst token bigger. One more bias that
runs in the user's favour, named rather than hidden.

  usage:  season_burst_census.py <games> <tag>[,<tag>...]
          tags are deck_compare arm directories under logs/deckcmp/Fungus/<tag>/Fungus.txt
"""
import collections
import json
import os
import subprocess
import sys

ROOT = "/workspaces/MagicDeckTester"
PROF = "logs/deckcmp/Fungus/aliased_at/Fungus.profile.json"
SEED = 1400000          # disjoint from every screen block (1180000-1329999) and census (1230000+)
SEASON, BURST = "Doubling Season", "Saproling Burst"
TURNS = (4, 5, 6, 7, 8)

GAMES = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
TAGS = (sys.argv[2] if len(sys.argv) > 2 else "s_ref,s_s3").split(",")


def run(tag):
    d = os.path.join(ROOT, "logs/season_burst_census", tag)
    os.makedirs(d, exist_ok=True)
    for f in os.listdir(d):
        os.remove(os.path.join(d, f))
    deck = os.path.join(ROOT, f"logs/deckcmp/Fungus/{tag}/Fungus.txt")
    if not os.path.exists(deck):
        raise SystemExit(f"no materialised decklist for {tag!r} at {deck}\n"
                         "  run the round that defines it first -- deck_compare writes the arm "
                         "decklists under logs/deckcmp/Fungus/<tag>/")
    cmd = [os.path.join(ROOT, "build/Release/mtg"), deck,
           "--cards-json", os.path.join(ROOT, "src/cards/data/cards.json"),
           "--games", str(GAMES), "--seed", str(SEED), "--max-turns", "8",
           "--depth", "7", "--budget-ms", "1000", "--threads", "32",
           "--profile", os.path.join(ROOT, PROF), "--log-dir", d]
    subprocess.run(cmd, cwd=ROOT, check=True,
                   stdout=open(os.path.join(ROOT, f"logs/season_burst_census/{tag}.run"), "w"),
                   stderr=subprocess.STDOUT)
    return d


def analyse(tag, d):
    n = 0
    armed = 0                                   # Season + Burst on the battlefield together
    first_arm = collections.Counter()
    bodies = {t: [] for t in TURNS}             # non-land permanents we control, by turn
    bodies_armed = {t: [] for t in TURNS}       # ...in games that armed by that turn
    bodies_unarmed = {t: [] for t in TURNS}
    first_season = collections.Counter()    # the TURN a Season first hit the battlefield
    first_burst = collections.Counter()     # ...and a Burst, so the USER's "Season T3, Burst T4"
    # *** THE DECISIVE CONDITIONAL ***  USER: "if Season lands T3 and Saproling Burst T4 the
    # opponent can no longer attack without evasion ... And the turn after you can finish them off."
    # That plan ends in a T5 KILL, which is squarely inside what avg-win-turn rewards -- so if the
    # 3rd Season still prices as a cost, the explanation must be that the plan is RARE rather than
    # weak, and the remedy is more acceleration rather than fewer Seasons. Splitting win turn by
    # whether the plan actually armed is what tells those two apart.
    win_armed, win_unarmed = [], []
    season_seen = burst_seen = 0
    season_drawn = 0          # Season reached HAND at any point -- the denominator for the cast rate
    life = []
    for fn in sorted(os.listdir(d)):
        if not fn.endswith(".json"):
            continue
        g = json.load(open(os.path.join(d, fn)))
        n += 1
        saw_s = saw_b = drew_s = False
        armed_by = {}
        is_armed = False
        for t in g.get("turns", []):
            b = t.get("boardAfter") or {}
            bf = b.get("battlefield") or []
            names = [p.get("cardName") for p in bf]
            # DRAWN vs CAST. The hand is logged too, so a Season that was held and never played is
            # distinguishable from one that was never seen. That difference is the point: round 31's
            # preflight found 52 of 52 drawn-and-never-cast games held >= 9 potential mana against a
            # cost of 5, i.e. the engine REFUSES to cast it -- because casting it buys WIDTH, and
            # width is worth nothing against an opponent that never blocks. If the refusal is still
            # there, an extra copy is a near-dead card in this apparatus for that reason alone, and
            # the arming rate below is a measure of the blind spot rather than of the decklist.
            drew_s = drew_s or any(c.get("cardName") == SEASON
                                   for c in (b.get("hand") or []))
            if SEASON in names and not saw_s:
                first_season[t.get("turn")] += 1
            if BURST in names and not saw_b:
                first_burst[t.get("turn")] += 1
            saw_s = saw_s or SEASON in names
            saw_b = saw_b or BURST in names
            if SEASON in names and BURST in names:
                if not is_armed:
                    is_armed = True
                    first_arm[t.get("turn")] += 1
            turn = t.get("turn")
            if turn in bodies:
                k = sum(1 for p in bf if not p.get("isLand"))
                bodies[turn].append(k)
                armed_by[turn] = is_armed
                (bodies_armed if is_armed else bodies_unarmed)[turn].append(k)
            if "playerLife" in b:
                last_life = b["playerLife"]
        res = g.get("result") or {}
        wt = res.get("winTurn", res.get("turn")) if isinstance(res, dict) else None
        if wt is None:
            wt = (g["turns"][-1].get("turn") if g.get("turns") else None)
        won = (res.get("won") if isinstance(res, dict) else None)
        if wt is not None:
            # unwon games are scored max_turns+1 = 9, the repo's primary objective (deck_compare's
            # score(): "Unwon prints wt=-1 and is scored max_turns+1")
            v = 9 if won is False else wt
            (win_armed if is_armed else win_unarmed).append(v)
        season_drawn += (drew_s or saw_s)
        season_seen += saw_s
        burst_seen += saw_b
        armed += is_armed
        life.append(last_life)
    mean = lambda xs: sum(xs) / len(xs) if xs else float("nan")
    print(f"\n=== {tag}   ({n} games, seed {SEED}, d7/b1000) ===")
    print(f"  Doubling Season was DRAWN (hand or board)    {season_drawn:>6}  "
          f"({season_drawn / max(1, n):6.2%})")
    print(f"  Doubling Season reached the BATTLEFIELD       {season_seen:>6}  "
          f"({season_seen / max(1, n):6.2%})"
          + (f"   -> CAST RATE {season_seen / season_drawn:6.1%} of games that drew it"
             if season_drawn else ""))
    print(f"  Saproling Burst reached the battlefield      {burst_seen:>6}  "
          f"({burst_seen / max(1, n):6.2%})")
    print(f"  *** BOTH AT ONCE (the arming condition)      {armed:>6}  "
          f"({armed / max(1, n):6.2%}) ***")
    def dist(lbl, c, denom):
        if not c:
            return
        ks = sorted(k for k in c if k is not None)
        print(f"  {lbl:<44}" + "  ".join(f"T{k}:{c[k]/denom:.1%}" for k in ks))
        tot = sum(c[k] for k in ks)
        if tot:
            print(f"  {'   ...mean landing turn, when it lands':<44}"
                  f"{sum(k*c[k] for k in ks)/tot:.2f}")
    dist("turn a Doubling Season first LANDED", first_season, max(1, n))
    dist("turn a Saproling Burst first LANDED", first_burst, max(1, n))
    if first_arm:
        print("  first turn both were out: "
              + ", ".join(f"T{k}:{v}" for k, v in sorted(first_arm.items()) if k is not None))
    print(f"  mean BODIES we control (non-land permanents), and conditional on having armed:")
    print(f"    {'turn':>6} {'all games':>11} {'ARMED':>9} {'not armed':>11} {'armed lift':>11}")
    for t in TURNS:
        a, u = mean(bodies_armed[t]), mean(bodies_unarmed[t])
        print(f"    {t:>6} {mean(bodies[t]):>11.2f} {a:>9.2f} {u:>11.2f} "
              f"{(a - u):>11.2f}")
    print(f"  *** WIN TURN, split by whether the plan ARMED (lower = faster) ***")
    print(f"      armed     {mean(win_armed):6.4f}   (n={len(win_armed)})")
    print(f"      not armed {mean(win_unarmed):6.4f}   (n={len(win_unarmed)})")
    if win_armed and win_unarmed:
        print(f"      ARMED IS FASTER BY {mean(win_unarmed) - mean(win_armed):+.4f} turns"
              "   <- Season+Burst's value INSIDE the metric")
    print(f"  mean final life (for the Ancient Tomb comparison): {mean(life):.2f}")
    return {"games": n, "armed": armed / max(1, n),
            "drawn": season_drawn / max(1, n),
            "cast_rate": (season_seen / season_drawn) if season_drawn else float("nan"),
            "season_turn": (sum(k*v for k, v in first_season.items() if k is not None)
                            / max(1, sum(v for k, v in first_season.items() if k is not None))),
            "bodies": {t: mean(bodies[t]) for t in TURNS},
            "life": mean(life),
            "win_armed": mean(win_armed), "win_unarmed": mean(win_unarmed),
            "n_armed": len(win_armed)}


if __name__ == "__main__":
    out = {}
    for tag in TAGS:
        out[tag] = analyse(tag, run(tag))
    if len(TAGS) > 1:
        base = TAGS[0]
        print(f"\n=== DELTAS vs {base} ===")
        for tag in TAGS[1:]:
            da = (out[tag]["armed"] - out[base]["armed"]) * 100
            print(f"  {tag:<16} drawn {(out[tag]['drawn']-out[base]['drawn'])*100:+6.2f} pts  "
                  f"cast-rate {out[tag]['cast_rate']:6.1%}  "
                  f"arming {da:+6.2f} pts  "
                  f"Season lands T{out[tag]['season_turn']:.2f} "
                  f"({out[tag]['season_turn']-out[base]['season_turn']:+.2f})   "
                  + "  ".join(f"T{t} bodies {out[tag]['bodies'][t] - out[base]['bodies'][t]:+5.2f}"
                              for t in TURNS)
                  + f"   life {out[tag]['life'] - out[base]['life']:+6.2f}")
        print()
        for tag in TAGS:
            o = out[tag]
            print(f"  {tag:<16} armed n={o['n_armed']:<6} win(armed) {o['win_armed']:.4f}  "
                  f"win(not) {o['win_unarmed']:.4f}  lift {o['win_unarmed']-o['win_armed']:+.4f}")
        print("\n  READ IT THIS WAY: the arming delta is what a 3rd Doubling Season actually BUYS --\n"
              "  the extra fraction of games in which the Season + Saproling Burst plan comes\n"
              "  together at all. Weigh it against the SPEED cost the screen measured (+0.0239 std /\n"
              "  +0.0251 2hg for the 3rd copy). The body counts are the 'huge boards' claim as a\n"
              "  number, and they UNDERSTATE it: token POWER is not in the log, and Season makes each\n"
              "  Burst token bigger as well as more numerous.")

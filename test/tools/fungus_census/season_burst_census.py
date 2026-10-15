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
PLIVES = "Parallel Lives"
# BOTH DOUBLERS ARE TRACKED, because the USER's case for Parallel Lives is a CASTABILITY case and the
# only way to settle it is the landing-turn distribution (USER 2026-10-15: *"it might be worth losing
# the counter doubling for the additional castability, especially when we have 7 5-drops. For example
# T2 is much easier for Parallel Lives and that would be a notable improvement."*).
#
# THE 7 FIVE-DROPS ARE REAL: Saproling Burst is {4}{G} and Doubling Season is {4}{G}, so at Season 3
# the deck holds 4 + 3 = 7 five-drops in 36 spells. Parallel Lives is {3}{G}.
#
# WHY T2 IS EASIER, PRECISELY -- and it is NOT "two Ancient Tombs", which is the trap: Ancient Tomb
# produces {C} only and EVERY spell in this deck needs exactly one GREEN pip, so Tomb+Tomb is four
# colourless mana that casts neither doubler. The real T2 lines that reach 4-with-green but not
# 5-with-green are:
#     T1 Forest + Wild Growth on it, T2 Ancient Tomb  -> {G}{G}{C}{C} = 4   PLives yes, Season no
#     T1 Forest + Sol Ring,          T2 Forest        -> {G}{G}+{C}{C} = 4   PLives yes, Season no
# Both routes run through cards the screen only just added (3 Wild Growth in round 32, 4 Ancient Tomb
# in round 34), so ADOPTING ANCIENT TOMB RAISED PARALLEL LIVES' RELATIVE VALUE: Tomb's colourless
# mana reaches 4-with-one-green far more often than it reaches 5-with-one-green.
DOUBLERS = (SEASON, PLIVES)
TURNS = (4, 5, 6, 7, 8)

GAMES = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
# The default pairs the Season-2 reference with the USER-ruled Season 3 (USER 2026-10-15: *"I think I
# will choose the 3rd season and go from there. We can leave out the 4th to reduce duplicates."*).
# `s_s3_thal` is the donor step the round-34 interaction is measured on; pass tags explicitly to
# census a different donor. A tag must be a round-34 arm directory that deck_compare has MATERIALISED
# -- this script reads the decklist off disk and does not build one.
TAGS = (sys.argv[2] if len(sys.argv) > 2 else "s_ref,s_s3_thal").split(",")


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
    first_dbl = {c: collections.Counter() for c in DOUBLERS}   # TURN each doubler first landed
    first_any = collections.Counter()       # ...and the turn ANY doubler first landed, which is the
                                            # number the castability argument actually turns on
    first_burst = collections.Counter()     # ...and a Burst, so the USER's "Season T3, Burst T4"
    # *** THE DECISIVE CONDITIONAL ***  USER: "if Season lands T3 and Saproling Burst T4 the
    # opponent can no longer attack without evasion ... And the turn after you can finish them off."
    # That plan ends in a T5 KILL, which is squarely inside what avg-win-turn rewards -- so if the
    # 3rd Season still prices as a cost, the explanation must be that the plan is RARE rather than
    # weak, and the remedy is more acceleration rather than fewer Seasons. Splitting win turn by
    # whether the plan actually armed is what tells those two apart.
    win_armed, win_unarmed = [], []
    burst_seen = 0
    seen = {c: 0 for c in DOUBLERS}    # reached the BATTLEFIELD
    drawn = {c: 0 for c in DOUBLERS}   # reached HAND or board -- the denominator for the cast rate
    any_seen = any_drawn = 0
    life = []
    for fn in sorted(os.listdir(d)):
        if not fn.endswith(".json"):
            continue
        g = json.load(open(os.path.join(d, fn)))
        n += 1
        saw_b = False
        saw = {c: False for c in DOUBLERS}
        drew = {c: False for c in DOUBLERS}
        saw_any = False
        armed_by = {}
        is_armed = False
        last_life = None
        # THE HAND IS LOGGED AS CARD INDICES, NOT OBJECTS. `boardAfter.battlefield` entries are dicts
        # carrying `cardName`, but `boardAfter.hand` is a bare list of ints (e.g. [49, 31, 14, 34])
        # keyed into the game's own `cardNumbering` map (name -> [indices]). Reading `.get("cardName")`
        # off a hand entry therefore raises AttributeError on the first game, which is exactly how this
        # script failed the first time it was ever run. Indices are PER GAME because deck_compare
        # rewrites numbering.json per arm, so the map must be rebuilt per file and never cached across
        # tags. Dict entries are still tolerated so a future log format change degrades rather than
        # crashes.
        idx2name = {i: name for name, idxs in (g.get("cardNumbering") or {}).items() for i in idxs}

        def hand_has(b, want):
            for c in (b.get("hand") or []):
                nm = c.get("cardName") if isinstance(c, dict) else idx2name.get(c)
                if nm == want:
                    return True
            return False
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
            for c in DOUBLERS:
                drew[c] = drew[c] or hand_has(b, c)
                if c in names and not saw[c]:
                    first_dbl[c][t.get("turn")] += 1
                saw[c] = saw[c] or c in names
            dbl_out = any(saw[c] for c in DOUBLERS)
            if dbl_out and not saw_any:
                first_any[t.get("turn")] += 1
            saw_any = saw_any or dbl_out
            if BURST in names and not saw_b:
                first_burst[t.get("turn")] += 1
            saw_b = saw_b or BURST in names
            # ARMED = ANY doubler plus a Burst. Generalised from Season-only so the Parallel Lives
            # arms are measured on the same condition rather than reading 0% by construction.
            if any(c in names for c in DOUBLERS) and BURST in names:
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
        # THE RESULT RECORD IS {"turn": N, "winner": "player"} -- there is no `won` and no `winTurn`
        # key. Asking for `won` returns None, and `None is False` is False, so the original read scored
        # an UNWON game as its own last turn instead of as a loss: a silent optimistic bias landing
        # entirely on the arms that fail to win, which is the opposite of what this census is for.
        # Censoring is small here (0.10% std / 0.25% 2hg unwon by turn 8) but a bias that small is
        # still a bias pointed the wrong way.
        res = g.get("result") if isinstance(g.get("result"), dict) else {}
        wt = res.get("winTurn", res.get("turn"))
        won = (res.get("won") if "won" in res else
               (res.get("winner") == "player") if "winner" in res else None)
        if wt is None:
            wt = (g["turns"][-1].get("turn") if g.get("turns") else None)
        # unwon games are scored max_turns+1 = 9, the repo's primary objective (deck_compare's
        # score(): "Unwon prints wt=-1 and is scored max_turns+1"). wt < 0 is the engine's own
        # unwon marker and must be caught even if `winner` is ever absent.
        if won is False or (wt is not None and wt < 0):
            (win_armed if is_armed else win_unarmed).append(9)
        elif wt is not None:
            (win_armed if is_armed else win_unarmed).append(wt)
        for c in DOUBLERS:
            drawn[c] += (drew[c] or saw[c])
            seen[c] += saw[c]
        any_drawn += any(drew[c] or saw[c] for c in DOUBLERS)
        any_seen += saw_any
        burst_seen += saw_b
        armed += is_armed
        if last_life is not None:
            life.append(last_life)
    mean = lambda xs: sum(xs) / len(xs) if xs else float("nan")
    print(f"\n=== {tag}   ({n} games, seed {SEED}, d7/b1000) ===")
    for c in DOUBLERS:
        if not drawn[c] and not seen[c]:
            continue        # the arm does not play this doubler at all
        print(f"  {c} was DRAWN (hand or board)".ljust(46)
              + f"{drawn[c]:>6}  ({drawn[c] / max(1, n):6.2%})")
        print(f"  {c} reached the BATTLEFIELD".ljust(46)
              + f"{seen[c]:>6}  ({seen[c] / max(1, n):6.2%})"
              + (f"   -> CAST RATE {seen[c] / drawn[c]:6.1%} of games that drew it"
                 if drawn[c] else ""))
    if sum(1 for c in DOUBLERS if drawn[c] or seen[c]) > 1:
        print(f"  ANY doubler reached the BATTLEFIELD".ljust(46)
              + f"{any_seen:>6}  ({any_seen / max(1, n):6.2%})"
              + (f"   -> CAST RATE {any_seen / any_drawn:6.1%}" if any_drawn else ""))
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
    for c in DOUBLERS:
        dist(f"turn a {c} first LANDED", first_dbl[c], max(1, n))
    # *** THE CASTABILITY NUMBER ***  This is the line the USER's Parallel Lives case rests on: a
    # 4-mana doubler should land EARLIER than a 5-mana one against 7 five-drops. Compare the mean
    # ANY-doubler landing turn across the p_s3 and p_pl arms, not the per-card lines, because the
    # arms hold different cards and the per-card means have different denominators.
    dist("turn ANY doubler LANDED <-- CASTABILITY", first_any, max(1, n))
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
        gap = mean(win_unarmed) - mean(win_armed)
        print(f"      armed games are {'FASTER' if gap > 0 else 'SLOWER'} by "
              f"{abs(gap):.4f} turns")
        # THE CONFOUND RUNS THE OPPOSITE WAY FROM THE ONE I FIRST NAMED, and it is the stronger of
        # the two. I had called this selection-confounded because a game that arms "drew both pieces
        # with mana to spare". The real and much larger effect is SURVIVORSHIP: arming needs a
        # FIVE-mana enchantment plus a Burst on the battlefield at once, so a game that kills on turn
        # 4 can never appear in the armed bucket. Conditioning on arming therefore conditions on NOT
        # having already won, which drags the armed mean up by construction. So a negative gap here
        # is NOT evidence that Season is bad, and a positive one would NOT be evidence that it is
        # good. THE CAUSAL ESTIMATE IS THE SCREEN'S ARM-LEVEL DELTA; the number this census actually
        # contributes is the ARMING RATE above -- how often the plan happens at all.
        print("      ^ SURVIVORSHIP-CONFOUNDED, not causal: arming requires the game to last long")
        print("        enough to deploy a 5-drop, so fast wins cannot enter the armed bucket.")
    print(f"  mean final life (for the Ancient Tomb comparison): {mean(life):.2f}")
    return {"games": n, "armed": armed / max(1, n),
            "drawn": any_drawn / max(1, n),
            "cast_rate": (any_seen / any_drawn) if any_drawn else float("nan"),
            # the ANY-doubler landing turn, so a Parallel Lives arm and a Season arm are compared on
            # the same quantity rather than on two cards' separate means
            "season_turn": (sum(k*v for k, v in first_any.items() if k is not None)
                            / max(1, sum(v for k, v in first_any.items() if k is not None))),
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
                  f"doubler lands T{out[tag]['season_turn']:.2f} "
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

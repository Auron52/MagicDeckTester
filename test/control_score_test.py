#!/usr/bin/env python3
"""Test of scripts/control_score.py, the control-scoring mode's scoring library
(docs/design/control-scoring-mode.md sections 4.1-4.6; docs/design/control-mode-stage2-report.md).

Covers: no effects gives E == W exactly; a lock on round L gives E == L; the section 4.3a worked table and its
two gauntlet mixes (3.5 and 4.65); the section 4.4 table (7.0, not 6.0 or 4.0); the section 4.3 properties as
seeded property tests (the lone-effect floor, E <= W, monotone in W, monotone in every D_k, a later effect never
raises E); LB <= E for every continuation of a line, in both forms; the chain form never above the prefix form;
the horizon / censoring rules of Q5; the race-win share; and the report end to end on a synthetic results file.
No engine, no build.

    python3 test/control_score_test.py
"""
import json
import math
import os
import random
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import control_score as cs  # noqa: E402

INF = math.inf
FAILS = []
EPS = 1e-9


def check(cond, what, detail=None):
    print(("ok    " if cond else "FAIL  ") + what)
    if not cond:
        FAILS.append(what)
        if detail is not None:
            print("      %s" % str(detail).replace("\n", "\n      "))


def close(a, b):
    return abs(a - b) <= EPS or (math.isinf(a) and math.isinf(b) and (a > 0) == (b > 0))


def rand_schedule(rng, n_max=4, w_max=16):
    n = rng.randint(0, n_max)
    rounds = sorted(rng.randint(1, 12) for _ in range(n))
    delays = [rng.choice([INF, 0.0, 0.5, 1.0, 2.0, 3.0, 6.0, -1.0, rng.uniform(-1, 8)]) for _ in range(n)]
    w = rng.randint(1, w_max)
    return w, rounds, delays


def test_exact_cases():
    for form in cs.FORMS:
        check(all(cs.pair_score(w, [], [], form) == w for w in range(1, 17)),
              "%s: no effects -> E == W exactly, W = 1..16" % form)
        # zero delays on every prefix also give E == W (the effect did nothing)
        check(cs.pair_score(9, [3, 4, 5], [0.0, 0.0, 0.0], form) == 9, "%s: effects with zero delay -> E == W" % form)
        # a lock on round L
        for L, w in ((5, 9), (3, 13), (4, 16), (6, 7)):
            check(cs.pair_score(w, [L], [INF], form) == min(w, L), "%s: lock on round %d, W=%d -> E == %d"
                  % (form, L, w, min(w, L)))
        # lock after an irrelevant earlier effect
        check(cs.pair_score(10, [2, 5], [0.0, INF], form) == 5, "%s: nothing on 2, lock on 5 -> 5" % form)
        # we won before the effect landed
        check(cs.pair_score(4, [6], [INF], form) == 4, "%s: W=4 before a round-6 lock -> 4" % form)
    # report-level: no effects anywhere -> E == W exactly, per game and overall
    res = {"horizon": 12, "games": [
        {"game": i, "on_the_play": i % 2 == 0, "w": w, "effects": [],
         "gauntlet": {"A": [{"seed": 1, "k": 4, "prefix_k": []}, {"seed": 3, "k": 6, "prefix_k": []}],
                      "B": [{"seed": 5, "k": None, "prefix_k": []}]}}
        for i, w in enumerate([5, 7, None, 13, 9])]}
    rep = cs.score_report(res)
    check(close(rep["E"], rep["W"]), "report: no control effects -> E == raw W exactly (%.4f)" % rep["E"])
    check(close(rep["W"], (5 + 7 + 13 + 13 + 9) / 5), "report: unwon and a kill after 12 both score 13")


def test_table43a():
    rows, a, b = cs.table43a("prefix")
    for (lab, e), (_, _, _, want) in zip(rows, cs.TABLE_43A):
        check(e == want, "4.3a row: %s -> %g" % (lab, want), "got %g" % e)
    check(close(a, 3.5), "4.3a creature-heavy mix -> 3.5", a)
    check(close(b, 4.65), "4.3a mixed gauntlet 40/30/15/15 -> 4.65", b)
    # the chain form: row 2 drops to 3 (the Wave's 2-round delay lets us survive to Opalescence)
    rows_c, ac, bc = cs.table43a("chain")
    check([e for _, e in rows_c] == [3.0, 3.0, 6.0, 9.0], "4.3a chain form rows -> 3, 3, 6, 9",
          [e for _, e in rows_c])
    check(close(ac, 3.0) and close(bc, 4.35), "4.3a chain form mixes -> 3.0 and 4.35", (ac, bc))


def test_table44():
    check(close(cs.table44(), 7.0), "4.4: per pair then average -> 7.0", cs.table44())
    # The two orders the doc rejects. NOTE (doc erratum): section 4.4's capped row prints 6.0, but with its own
    # numbers (mean D = (12 + 0) / 2 = 6) the formula gives max(4, 10 - 6) = 4.0, the same as uncapped. The point
    # stands: averaging first gives 4.0, the per-pair score is 7.0.
    avg_d_capped = cs.pair_score(10, [4], [(12.0 + 0.0) / 2])
    check(avg_d_capped == 4.0, "4.4: average-D-first with the 12-round cap gives 4.0 (doc prints 6.0; rejected)")
    check(cs.pair_score(10, [4], [(INF + 0.0) / 2]) == 4.0, "4.4: average-D-first uncapped gives 4.0 (rejected)")
    # the two-level mean is NOT the pooled mean when decks have unequal game counts
    two, per = cs.two_level_mean({"A": [4.0], "B": [10.0, 10.0, 10.0]})
    check(close(two, 7.0) and close(per["B"], 10.0), "two-level mean weights decks equally (7.0, not 8.5)")


def test_properties(trials=20000):
    rng = random.Random(20261127)
    bad = {k: None for k in ("floor", "le_w", "mono_w", "mono_d", "later", "chain_le_prefix")}
    for _ in range(trials):
        w, rounds, delays = rand_schedule(rng)
        for form in cs.FORMS:
            e = cs.pair_score(w, rounds, delays, form)
            if e > w + EPS and bad["le_w"] is None:
                bad["le_w"] = (form, w, rounds, delays, e)
            # monotone in our speed: a faster W never raises E
            if w > 1:
                e2 = cs.pair_score(w - 1, rounds, delays, form)
                if e2 > e + EPS and bad["mono_w"] is None:
                    bad["mono_w"] = (form, w, rounds, delays, e, e2)
            # monotone in delay: raising any D_k never raises E
            if rounds:
                i = rng.randrange(len(rounds))
                d2 = list(delays)
                d2[i] = d2[i] + rng.choice([0.5, 1.0, 3.0, INF])
                e3 = cs.pair_score(w, rounds, d2, form)
                if e3 > e + EPS and bad["mono_d"] is None:
                    bad["mono_d"] = (form, w, rounds, delays, d2, e, e3)
            # a later effect only adds a term: never raises E (the existing prefixes' delays are unchanged)
            t_new = rng.randint(rounds[-1] if rounds else 1, 14)
            e4 = cs.pair_score(w, rounds + [t_new], delays + [rng.choice([INF, 0.0, 2.0, -1.0])], form)
            if e4 > e + EPS and bad["later"] is None:
                bad["later"] = (form, w, rounds, delays, t_new, e, e4)
        # the lone-effect floor (USER: "a spell cast on Turn 5 on its own should never reduce ... below Turn 5")
        t = rng.randint(1, 12)
        d = rng.choice([INF, 0.0, 1.0, 2.5, 10.0, -2.0])
        for form in cs.FORMS:
            e1 = cs.pair_score(w, [t], [d], form)
            if e1 < min(w, t) - EPS and bad["floor"] is None:
                bad["floor"] = (form, w, t, d, e1)
        # chain <= prefix whenever every delay is >= 0
        dpos = [max(0.0, x) for x in delays]
        if cs.pair_score(w, rounds, dpos, "chain") > cs.pair_score(w, rounds, dpos, "prefix") + EPS \
                and bad["chain_le_prefix"] is None:
            bad["chain_le_prefix"] = (w, rounds, dpos)
    check(bad["floor"] is None, "property: a lone effect on round T never scores below min(W, T)", bad["floor"])
    check(bad["le_w"] is None, "property: never better than winning, E <= W", bad["le_w"])
    check(bad["mono_w"] is None, "property: monotone in our speed (lower W never raises E)", bad["mono_w"])
    check(bad["mono_d"] is None, "property: monotone in delay (raising any D_k never raises E)", bad["mono_d"])
    check(bad["later"] is None, "property: a later effect never raises E", bad["later"])
    check(bad["chain_le_prefix"] is None, "property: chain form <= prefix form for non-negative delays",
          bad["chain_le_prefix"])


def test_lower_bound(trials=4000):
    """LB(line, t) <= E for EVERY continuation: later effects on rounds >= t with arbitrary delays (including
    ones where a longer prefix delays LESS), and any kill round W >= t. Both forms."""
    rng = random.Random(77)
    worst = {f: None for f in cs.FORMS}
    count = 0
    for _ in range(trials):
        t = rng.randint(1, 12)
        m = rng.randint(0, 3)
        rounds = sorted(rng.randint(1, t) for _ in range(m))
        decks = {}
        for d in range(rng.randint(1, 3)):
            decks["g%d" % d] = [[rng.choice([INF, 0.0, 1.0, 2.0, 5.0, -1.0, rng.uniform(0, 6)]) for _ in range(m)]
                                for _ in range(rng.randint(1, 4))]
        for form in cs.FORMS:
            lb = cs.lower_bound(t, rounds, decks, form)
            for _c in range(6):
                extra = sorted(rng.randint(t, 15) for _ in range(rng.randint(0, 3)))
                w = rng.randint(t, 16)
                cont = {d: [ds + [rng.choice([INF, 0.0, 0.5, 3.0, 9.0, -2.0, rng.uniform(-1, 10)])
                                  for _ in extra] for ds in games]
                        for d, games in decks.items()}
                e = cs.line_score(w, rounds + extra, cont, form)
                count += 1
                if e < lb - EPS and worst[form] is None:
                    worst[form] = (t, rounds, decks, extra, w, cont, lb, e)
    for form in cs.FORMS:
        check(worst[form] is None, "LB <= E for every continuation (%s form, %d continuations)" % (form, count // 2),
              worst[form])
    # goldfish's rule is the special case: no effects -> LB == t
    check(cs.lower_bound(6, [], {"A": [[]], "B": [[], []]}) == 6.0, "LB with no effects == the current round")
    # and the prefix-form LB is attained by winning right now
    decks = {"A": [[INF], [0.0]], "B": [[2.0]]}
    check(close(cs.lower_bound(5, [3], decks), cs.line_score(5, [3], decks)), "prefix LB == score of winning now")
    # chain form: a FUTURE term can sit below t (a lock on round 7 after a 4-round delay on round 5 has survival
    # part 7 - 4 = 3 < t = 6), yet the continuation stays above LB, because the last existing term bounds it
    decks = {"A": [[4.0]]}
    lb_c = cs.lower_bound(6, [5], decks, "chain")
    e_c = cs.line_score(9, [5, 7], {"A": [[4.0, INF]]}, "chain")
    terms = cs.pair_terms(9, [5, 7], [4.0, INF], "chain")
    check(terms[-1] < 6 and lb_c <= e_c + EPS, "chain form: a future term below t still respects LB "
          "(future term %.1f, LB %.1f, continuation %.1f)" % (terms[-1], lb_c, e_c))


def test_horizon_and_censoring():
    check(cs.censor_w(None, 12) == 13 and cs.censor_w(14, 12) == 13 and cs.censor_w(14, 15) == 14
          and cs.censor_w(None, 15) == 16, "W: unwon -> H+1; a kill after H -> H+1")
    for bad in (11, 16, 12.0):
        try:
            cs.check_horizon(bad)
            ok = False
        except ValueError:
            ok = True
        check(ok, "horizon %r refused (12..15 ints only)" % (bad,))
    check(cs.delay(4, None) == INF, "delayed game unwon within 15 -> a lock (D = inf)")
    check(cs.delay(4, None, "bound") == 12.0, "censored='bound': D >= 16 - K")
    check(cs.delay(None, None) == 0.0 and cs.delay(None, 9) == 0.0, "undisrupted game unwon within 15 -> no credit")
    check(cs.delay(5, 4) == -1.0, "a faster disrupted game gives a negative delay")
    check(cs.pair_score(9, [3], [-1.0]) == 9, "a negative delay never helps (the W term wins the min)")
    check(close(cs.unwon_share([5, None, 13, 12, 15], 12), 3 / 5), "unwon share at 12 counts None and > 12")


def test_race_win():
    # on the play: win iff K >= E; on the draw: iff K > E; never a win when E is the unwon score
    check(cs.race_win(5, 5, True, 12) and not cs.race_win(5, 5, False, 12), "race: K == E wins on the play only")
    check(cs.race_win(5, None, False, 12), "race: an undisrupted no-kill game is always won by a kill in H")
    check(not cs.race_win(13, None, True, 12), "race: an unwon score (H+1) is never a race win")
    # race win <=> K >= E (play): check against a direct race simulation on random pairs, prefix form
    rng = random.Random(5)
    mism = None
    for _ in range(5000):
        w, rounds, delays = rand_schedule(rng, w_max=12)
        delays = [max(0.0, d) for d in delays]
        k = rng.randint(1, 12)
        e = cs.pair_score(w, rounds, delays, "prefix")
        # the section 4.2 race (as derived there): some prefix k with K >= T_k and K + D_k >= W (k = 0: K >= W)
        won = k >= w or any(k >= t and k + d >= w for t, d in zip(rounds, delays))
        if won != cs.race_win(e, k, True, 12) and mism is None:
            mism = (w, rounds, delays, k, e)
    check(mism is None, "race: K >= E(prefix) is exactly the section 4.2 race condition", mism)
    mism = None
    for _ in range(5000):
        w, rounds, delays = rand_schedule(rng, w_max=12)
        delays = [max(0.0, d) for d in delays]
        k = rng.randint(1, 12)
        e = cs.pair_score(w, rounds, delays, "chain")
        # the survival-chained race: alive at every T_j under prefix j-1, and K + D_k >= W
        won = k >= w
        prev = 0.0
        alive = True
        for t, d in zip(rounds, delays):
            alive = alive and k + prev >= t
            if alive and k + d >= w:
                won = True
            prev = d
        if won != cs.race_win(e, k, True, 12) and mism is None:
            mism = (w, rounds, delays, k, e)
    check(mism is None, "race: K >= E(chain) is exactly the survival-chained race condition", mism)


def test_report_end_to_end():
    res = {"horizon": 12, "games": [
        {"game": 0, "on_the_play": True, "w": 9,
         "effects": [{"round": 3, "kind": "removal", "card": "Parallax Wave"},
                     {"round": 4, "kind": "lock_piece", "card": "Opalescence"}],
         "gauntlet": {
             "Creature": [{"seed": 1, "k": 5, "prefix_k": [11, None]}, {"seed": 3, "k": 5, "prefix_k": [7, None]}],
             "Burn": [{"seed": 1, "k": 4, "prefix_k": [4, 4]}]},
         "without": {"lock_piece": {
             "Creature": [{"seed": 1, "k": 5, "prefix_k": [11]}, {"seed": 3, "k": 5, "prefix_k": [7]}],
             "Burn": [{"seed": 1, "k": 4, "prefix_k": [4]}]}}},
    ]}
    rep = cs.score_report(res)
    check(close(rep["per_deck"]["Creature"], 3.5) and close(rep["per_deck"]["Burn"], 9.0),
          "report: per-deck partials 3.5 (creature) and 9.0 (burn)", rep["per_deck"])
    check(close(rep["E"], (3.5 + 9.0) / 2), "report: E is the equal-weight mean of the deck partials", rep["E"])
    check(close(rep["race"]["Creature"], 1.0) and close(rep["race"]["Burn"], 0.0)
          and close(rep["race_none"]["Creature"], 0.0), "report: race-win share with and without control",
          (rep["race"], rep["race_none"]))
    check(close(rep["e_without"]["lock_piece"], (cs.mean([3.0, 7.0]) + 9.0) / 2),
          "report: E without the lock piece uses the filtered schedule's own delays", rep["e_without"])
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as fh:
        json.dump(res, fh)
        path = fh.name
    try:
        r = subprocess.run([sys.executable, os.path.join(ROOT, "scripts", "control_score.py"), "score", path],
                           capture_output=True, text=True)
        check(r.returncode == 0 and "beside raw W 9.000" in r.stdout, "CLI score prints E beside raw W",
              r.stdout + r.stderr)
        r = subprocess.run([sys.executable, os.path.join(ROOT, "scripts", "control_score.py"), "table43a"],
                           capture_output=True, text=True)
        check(r.returncode == 0 and "3.50" in r.stdout and "4.65" in r.stdout and "7.0" in r.stdout,
              "CLI table43a prints 3.50, 4.65 and 7.0", r.stdout + r.stderr)
    finally:
        os.unlink(path)


def test_classifier():
    """scripts/control_card_class.py on the real cards.json and decklists (data-derived, no engine)."""
    import control_card_class as cc
    cards = cc.load_cards()
    ee = {r["card"]: r for r in cc.classify_deck("decks/Enchanted Evening", cards)["rows"]}
    want = {"Parallax Wave", "Opalescence", "Enchanted Evening", "Calming Verse", "Force of Vigor", "Oblivion Ring"}
    got = {n for n, r in ee.items() if r["control_based"]}
    check(got == want, "classifier: Enchanted Evening's control-based cards", sorted(got ^ want))
    kinds = lambda r: {f["kind"] for f in r["findings"] if f["role"] == "primary"}
    check("land_destruction" in kinds(ee["Force of Vigor"]), "classifier: Force of Vigor under Enchanted Evening "
          "also destroys lands (deck context)")
    check(not ee["Sterling Grove"]["control_based"], "classifier: Sterling Grove's shroud is incidental only")
    hi = {r["card"]: r for r in cc.classify_deck("decks/Hinata", cards)["rows"]}
    check(kinds(hi["Remand"]) == {"counter_to_hand"} and kinds(hi["Memory Lapse"]) == {"counter_to_top"},
          "classifier: Remand is a tempo counter, Memory Lapse a control counter (USER 5.7)")
    check(not hi["Crackle with Power"]["control_based"] and hi["Crackle with Power"]["findings"],
          "classifier: burn that may hit a creature is incidental removal, not control-based")
    wf = {r["card"]: r for r in cc.classify_deck("decks/Worldfire", cards)["rows"]}
    check(not wf["Worldfire"]["control_based"] and wf["Worldfire"]["goldfish_credited"],
          "classifier: Worldfire is goldfish-credited (free-turn rule), so it does not gate a screen")
    bolt = cc.classify_card(cards["Lightning Helix"])
    check(not cc.is_control_based(bolt), "classifier: Lightning Helix's life gain is a burn spell's rider")


def main():
    test_exact_cases()
    test_table43a()
    test_table44()
    test_properties()
    test_lower_bound()
    test_horizon_and_censoring()
    test_race_win()
    test_report_end_to_end()
    test_classifier()
    print()
    if FAILS:
        print("%d FAILED" % len(FAILS))
        return 1
    print("all control_score checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

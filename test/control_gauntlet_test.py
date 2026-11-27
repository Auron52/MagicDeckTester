#!/usr/bin/env python3
"""Test of scripts/control_gauntlet.py, the control-scoring mode's stage-1 gauntlet drivers
(docs/design/control-inject-harness.md "Drivers"; docs/design/control-scoring-mode.md sections 7.5, 8).

Covers, on synthetic data: the suite parser (the play-setting row), the .wins / stdout / stderr parsers, the census
and the baseline table (kill round, damage curve, alternate-win marker, identity twins), the arm planner (round
screening, the clairvoyant index sweep and its name dedupe, joint arms, the life screen, the job's seed /
game_index / skip_games pairing), the delay convention (unwon = 16, an unwon base gives 0), the clairvoyant max,
the section 8.2 interaction and increment terms, the analytic skip check, the race-flip share, and the report end
to end. No engine, no build.

    python3 test/control_gauntlet_test.py
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import control_gauntlet as cg  # noqa: E402

FAILS = []


def check(cond, what, detail=None):
    print(("ok    " if cond else "FAIL  ") + what)
    if not cond:
        FAILS.append(what)
        if detail is not None:
            print("      %s" % str(detail).replace("\n", "\n      "))


def close(a, b, eps=1e-9):
    return a is not None and b is not None and abs(a - b) < eps


def test_suite_parser():
    text = "\n".join([
        "F\tburn\tdecks/Burn/Burn.txt\tdecks/Burn/Burn.profile.json",
        "F\tnoplay\tdecks/X/X.cod\tdecks/X/X.profile.json",
        "C\tburn 0 2002 1000 0", "C\tburn 3 2002 50 10", "C\tburn 5 2002 150 30", "C\tburn 5 3003 150 30",
        "C\tburn 5 30281 5 0", "C\tburn2hg 5 2002 75 20", "C\tnoplay 3 2002 50 10"])
    s = cg.parse_suite(text)
    check(set(s) == {"burn"}, "suite: a deck with no d5 budgeted row is not measurable at play settings", s)
    check(s["burn"]["budget"] == 30, "suite: the play budget is the d5 row's (b0 backstop and 2HG ignored)", s)
    seeds = cg.regression_seeds("burn 5 2002 150 30\nburn 5 3003 150 30\nburn 3 2002 50 10\n")
    check(seeds[("burn", 5, 30)] == [2002, 3003], "regression_seeds groups rows by (key, depth, budget)")


def test_parsers():
    w = cg.parse_wins("0 4 aa\n1 -1 bb\n2 -2 cc\n")
    check(w == {0: (4, "aa"), 1: (-1, "bb"), 2: (-2, "cc")}, "parse_wins", w)
    check([cg.kill_round(x) for x in (4, -1, -2, 15, 16)] == [4, None, None, 15, None],
          "kill_round: unwon / voided / past the horizon are None")
    out = cg.parse_stdout("=== BATCH ===\nburn__base: played=20 avg=4.5000 digest=0123abcd ms=900 units=77 cpu=1800\n")
    check(out == {"burn__base": {"played": 20, "avg": 4.5, "digest": "0123abcd", "ms": 900, "units": 77,
                                 "cpu": 1800}}, "parse_stdout", out)
    lines = ['[play] burn__base depth=6 budget=30ms source=x provider=Burn',
             '[ctrl-game] {"job":"burn__base","gi":0,"wt":3,"kill":"life","rounds":[]}',
             '[ctrl-inject] {"job":"burn__base","gi":0,"round":2,"effect":"x","n":0}',
             'noise']
    g, inj, play = cg.parse_stderr(lines)
    check(set(g) == {("burn__base", 0)} and len(inj["burn__base"]) == 1 and play == {"burn__base": (6, 30)},
          "parse_stderr: game records, inject records, the [play] depth and budget")


def synth_run():
    """A two-game baseline run: game 0 kills on round 3 through life; game 1 never kills."""
    def cg_line(gi, wt, kill, dmg, spells):
        rounds = [{"r": r + 1, "dmg": d, "spells": sp, "poison": 0} for r, (d, sp) in enumerate(zip(dmg, spells))]
        return "[ctrl-game] " + json.dumps({"job": "burn__base", "gi": gi, "wt": wt, "kill": kill,
                                            "through_life": kill == "life", "start_life": 20, "rounds": rounds})

    def census(gi, rnd, spec, cands, feats=None):
        return "[ctrl-inject] " + json.dumps({"job": "burn__base", "gi": gi, "round": rnd, "effect": spec, "n": 0,
                                              "candidates": cands, "count": len(cands), "applied": 0,
                                              "features": feats or {"lands_bf": len(cands)}})
    spec = dict((c, s) for c, s in cg.CENSUS)
    err = [cg_line(0, 3, "life", [3, 9, 22], [["Bolt"], ["Bolt", "Rift"], ["Bolt"]]),
           cg_line(1, -1, "none", [0] * 15, [[]] * 15),
           "[play] burn__base depth=6 budget=30ms source=x"]
    for gi, nr in ((0, 3), (1, 15)):
        for r in range(1, nr + 1):
            err.append(census(gi, r, spec["land"], ["Mountain"] * min(r, 3)))
            err.append(census(gi, r, spec["creature"], ["Swiftspear"] if r >= 2 else []))
            err.append(census(gi, r, spec["hand"], ["Bolt", "Bolt", "Mountain"]))
            err.append(census(gi, r, spec["untapped_land"], ["Mountain"] * min(r, 3)))
            err.append(census(gi, r, spec["untapped_creature"], ["Swiftspear"] if r >= 2 else []))
    meta = {"seed_base": 8100001, "src_tree": "t" * 40, "decks": {"burn": {"deck": "decks/Burn/Burn.txt",
            "profile": "decks/Burn/Burn.profile.json", "budget": 30}}}
    out = "burn__base: played=2 avg=10.0 digest=ab ms=10 units=5 cpu=400\n"
    wins = {"burn__base": {0: (3, "d0"), 1: (-1, "d1")}, "burn__plain": {0: (3, "d0"), 1: (-1, "dX")}}
    return meta, out, err, wins


def test_baseline_table():
    meta, out, err, wins = synth_run()
    tables, ident = cg.build_tables(meta, out, err, wins, False, {"burn": {"decklist_sha256": "h", "artifacts": {}}})
    t = tables["burn"]
    g0, g1 = t["games"]
    check(t["format"] == cg.FORMAT and t["stamp"]["n"] == 2 and t["stamp"]["depth"] == 6
          and t["stamp"]["budget_ms"] == 30 and t["stamp"]["play_digest"] == "ab"
          and t["stamp"]["decklist_sha256"] == "h" and t["stamp"]["seed_base"] == 8100001,
          "baseline table: format and stamp (depth from [play], play digest, decklist hash, seed set)", t["stamp"])
    check(g0["k"] == 3 and g0["seed"] == 8100001 and g0["otp"] is False and g0["dmg"] == [3, 9, 22]
          and g0["kill"] == "life" and g0["alt"] is False, "baseline game: kill round, seed/parity, damage curve", g0)
    check(g1["k"] is None and g1["otp"] is True and len(g1["dmg"]) == 15, "baseline game: unwon is k = None")
    check(g0["cands"]["land"] == [["Mountain"], ["Mountain"] * 2, ["Mountain"] * 3]
          and g0["cands"]["creature"][0] == [] and g0["spells"][1] == ["Bolt", "Rift"],
          "census: candidate lists per round, counter candidates from rounds[].spells", g0["cands"])
    check(ident == {"burn": (1, 2)}, "identity twin: one of two games differs -> reported, not hidden", ident)
    check(cg.summary_baseline(t)["mean_k"] == (3 + 16) / 2, "summary: unwon scored 16 in the mean kill round")
    return t


def test_planner(t):
    arms = {a["arm"]: a for a in cg.plan_arms(t, list(cg.KINDS), [2, 3, 4, 5])}
    check(arms["land4"]["run"] == [1] and arms["land4"]["zero"] == [0],
          "screen: a game won on round 3 is not run for a round-4 effect (delay 0 exactly)")
    check(arms["land3"]["run"] == [0, 1], "screen: a game won ON round 3 is still run for a round-3 effect")
    check("land3_i2" in arms and arms["land3_i2"]["run"] == [0, 1] and "land3_i3" not in arms,
          "clairvoyant: one arm per candidate index up to the largest count", sorted(a for a in arms if "land3" in a))
    check("disc2_i0" in arms and "disc2_i2" in arms and "disc2_i1" not in arms,
          "clairvoyant discard: a second copy of the same card in hand is not swept again")
    check("ctr3_i0" in arms and "ctr3_i1" not in arms and arms["ctr2_i1"]["run"] == [0],
          "clairvoyant counter: indices from the base game's round-t spells", sorted(a for a in arms if "ctr2" in a))
    check(arms["land3+4"]["spec"] == "destroy:3:land:0;destroy:4:land:0" and arms["land3+4"]["parts"] ==
          ["land3", "land4"], "joint arm: the fixed-policy specs joined, parts named like the singles")
    check(arms["xu3"]["spec"] == "exile_until:3:creature:big:ret=5" and arms["life3r4"]["spec"] == "life:4:3"
          and arms["tax2"]["spec"] == "tax:2-15:1", "fixed specs: exile_until ret = t + 2, life, tax to the horizon")
    job = cg.arm_job("burn", t, arms["land4"], {"deck": "D", "profile": "P"})
    check(job["seed"] == 8100002 and job["game_index"] == 1 and job["games"] == 1 and "skip_games" not in job
          and job["max_turns"] == 15 and "depth" not in job and job["budget_ms"] == 30,
          "arm job: seed = BASE + gi with game_index = gi, play setting (no depth key), horizon 15", job)
    arm = {"arm": "x", "run": [0, 3], "zero": [], "spec": "skip:3"}
    job = cg.arm_job("burn", dict(t, games=[]), arm, {"deck": "D", "profile": "P"})
    check(job["games"] == 4 and job["skip_games"] == [1, 2] and job["seed"] == 8100001,
          "arm job: a gap in the run list becomes skip_games (global indices)", job)
    g = {"k": 4, "kill": "life", "dmg": [3, 8, 14, 25], "start_life": 20}
    check(cg.life_zero(g, 3, 5) and not cg.life_zero(g, 3, 6) and cg.life_zero(dict(g, kill="poison"), 3, 9)
          and not cg.life_zero(dict(g, k=None), 3, 3) and cg.life_zero(g, 5, 9),
          "life screen: zero iff the recorded kill-round damage covers 20 + L, a non-life kill, or not reached")
    sc = {a["arm"]: a for a in cg.plan_arms(t, ["life3"], [2], False, False, True)}
    t2 = dict(t, games=[dict(t["games"][0], dmg=[3, 9, 30]), t["games"][1]])
    sc2 = {a["arm"]: a for a in cg.plan_arms(t2, ["life3"], [2], False, False, True)}
    check(sc["life3r2"]["zero"] == [] and sc2["life3r2"]["zero"] == [0] and sc2["life3r2"]["run"] == [1],
          "life screen in the planner: 22 < 23 is run, 30 >= 23 is zeroed, an unwon game is run")


def test_math(t):
    check(cg.game_delay(4, 6) == 2 and cg.game_delay(4, None) == 12 and cg.game_delay(None, None) == 0
          and cg.game_delay(None, 5) == 0 and cg.game_delay(5, 4) == -1,
          "delay: unwon arm = 16 - K; unwon base = 0 (nothing to delay); a faster re-plan is negative")
    arm = {"arm": "land4", "run": [1], "zero": [0]}
    d = cg.arm_delays(t, arm, {1: (5, "x")})
    check(d == {0: 0.0, 1: 0.0}, "arm_delays: zero games 0; an unwon base game gives 0 whatever the arm", d)
    d = cg.arm_delays(t, {"arm": "a", "run": [0, 1], "zero": []}, {0: (5, "x")})
    check(d == {0: 2.0}, "arm_delays: a run game missing from .wins is left out, never counted 0", d)
    sw = {0: {0: 1.0, 1: 0.0}, 1: {0: 3.0, 1: 0.0}, 2: {0: 2.0, 1: 0.0}}
    cl = cg.clairvoyant(t, "land", 3, sw, {0: 1.0, 1: 0.0})
    check(cl == {0: 3.0, 1: 0.0}, "clairvoyant: the max over the candidate indices, per game", cl)
    cl = cg.clairvoyant(t, "crt", 2, {0: {0: 2.0}}, None)
    check(cl[0] == 2.0, "clairvoyant: one creature, one index")
    m, se, n = cg.mean_se([1, 2, 3])
    check(close(m, 2) and close(se, 1 / 3 ** 0.5) and n == 3, "mean_se")
    m, se, n = cg.mean_over_decks([(1.0, 0.3), (2.0, 0.4)])
    check(close(m, 1.5) and close(se, 0.25), "mean over decks: equal weights, se = sqrt(sum se^2) / D")
    delays = {"land3": {0: 1, 1: 0, 2: 2}, "land4": {0: 0, 1: 1, 2: 1}, "land3+4": {0: 2, 1: 1, 2: 5},
              "land3+4+5": {0: 3, 1: 1, 2: 6}, "ctr3": {0: 1, 1: 1, 2: 1}, "ctr3+3": {0: 1, 1: 1, 2: 2},
              "sweep3": {0: 2, 1: 0, 2: 2}, "crt3": {0: 1, 1: 0, 2: 1}}
    ir = cg.interaction_rows(delays)
    check(close(ir["land: (3+4) - 3 - 4"][0], (1 + 0 + 2) / 3), "interaction: joint minus the sum, per game", ir)
    check(close(ir["ctr: two in round 3 - 2 x one"][0], (-1 - 1 + 0) / 3), "interaction: same-round pair minus 2x")
    check(close(ir["land: 3rd increment - 2nd"][0], ((1 - 1) + (0 - 1) + (1 - 3)) / 3), "increment: (c-b)-(b-a)")
    check(close(ir["sweep3 - crt3"][0], 2 / 3), "sweep3 - crt3")
    check("crt: (3+4) - 3 - 4" not in ir, "interaction: a row with a missing arm is omitted, not zero")
    skip = cg.skip_check(t, 3, {0: (4, "x"), 1: (-1, "y")})
    check(skip == (2, 2, []), "skip check: +1 for a game killing on the round, unwon stays unwon", skip)
    skip = cg.skip_check(t, 3, {0: (3, "x")})
    check(skip[0] == 0 and skip[2] == [(0, 3, 3)], "skip check: an exception is listed")
    check(cg.race_flip(t, {0: 2.0, 1: 0.0}, 4) == 1.0 and cg.race_flip(t, {0: 0.0}, 4) == 0.0
          and cg.race_flip(t, {0: 2.0}, 6) == 0.0, "race flip: K < w <= K + D over games that kill")


def test_report_end_to_end(t):
    arms = cg.plan_arms(t, ["land", "skip", "ctr"], [3, 4], True, True)
    meta = {"arms": {"burn": arms}, "life_screen": False}
    wins = {}
    for a in arms:
        w = {}
        for gi in a["run"]:
            base = t["games"][gi]["k"]
            if a["kind"] == "skip":
                w[gi] = (base + 1 if base else -1, "s")
            else:
                w[gi] = ((base + 1) if base and a["policy"] != "clair" else (base or -1), "x")
        wins["burn__" + a["arm"]] = w
    inj = {"burn": [{"job": "burn__land3", "gi": 0, "n": 0, "applied": 1, "count": 3, "features": {}}]}
    rep, rows = cg.build_report(meta, {"burn": t}, wins, inj)
    d = rep["decks"]["burn"]
    check(close(d["fixed"]["land3"]["mean"], 0.5) and d["hit"]["land3"] == (1, 2),
          "report: fixed delay mean and hit rate", d["fixed"]["land3"])
    check(close(d["clair"]["land3"]["mean"], 0.5), "report: clairvoyant >= fixed (fixed folded into the max)")
    check(rep["skip"]["burn"]["skip3"]["exact"] == 2, "report: skip check")
    check(rows and rows[0]["features"] == {} and rows[0]["arm"] == "land3", "report: state-feature rows")
    txt = cg.format_report(rep, meta)
    check("S[e,t,g]" in txt and "land3" in txt and "probe5" in txt, "report: formats")
    check(rep["life"]["burn"]["class"] in ("burn", "mixed", "creature", "combo"), "report: deck class assigned")


def main():
    test_suite_parser()
    test_parsers()
    t = test_baseline_table()
    test_planner(t)
    test_math(t)
    test_report_end_to_end(t)
    print("\n%d FAIL" % len(FAILS) if FAILS else "\nall passed")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())

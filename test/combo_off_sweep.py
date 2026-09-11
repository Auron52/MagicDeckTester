#!/usr/bin/env python3
"""COMBO OFF SWEEP -- does the button fire where it should, and does the click WIN?

USER, 2026-09-10: *"We should do a bunch of testing of Combo Off states and fix any case that
fails to go off."*  And, on what the machinery is FOR: *"Especially as the intention is to use the
same logic to skip work for the search."*

The standing doctrine this measures against (USER): **display must be AGGRESSIVE** -- fire wherever
the rule holds -- and **execution must be EXACT** -- a click wins.  Those are two different
questions and this harness asks them separately, over three state populations, and sorts every
answer into one of five classes:

    a  offered + apply WINS                    -- good
    b  offered + apply does NOT win            -- EXECUTOR FAILURE  (the user's target)
    c  not offered + the position DOES win     -- MISSED OFFER      (rule too tight)
    d  offered + provably unwinnable           -- RULE TOO LOOSE    (report, never silently narrow)
    e  not offered + unwinnable                -- good

Class (d) is REPORTED, never acted on: narrowing the display is the user's ruling, not ours.

--- THE THREE POPULATIONS ------------------------------------------------------------------------

  synthetic   a combinatorial matrix over the rule table's own ingredients (outlet x untapper x
              {C} count x U/B x draw source x finisher x Training Grounds x Shivan Gorge x land
              yield).  Full control, so a failure here is a MINIMAL repro by construction.
  reference   every main-phase frame of the user's own saved games (references/<deck>/*.json).
              Real boards the user actually sat in front of.
  engine      every main-phase frame of AUTONOMOUS games at the deck's shipped settings
              (`--log-dir`, no --depth/--budget-ms so the deck's own value_play policy governs).
              THIS is the population the search-shortcut question is about: if the rule table is
              ever going to let the search skip a go-off turn, these are the states it will be
              asked on, and a FALSE FIRE here would corrupt evaluation and ground truth.

--- HOW A STATE IS ASKED -------------------------------------------------------------------------

Every state becomes a `--scenario` fixture (the same shape as test/combo_off/*.json) and is put to
the engine twice:

  offer probe   `expect_combo_off: true` + MTG_HUMAN_PLAY=1.  Runs TurnSolver::EnumerateMainPlans --
                the viewer's own entry point -- and then RE-APPLIES the flagged plan through the
                public ApplyPlan under ComboOffApplyPause, exactly as a click does.  Reports
                offered / rule / verified / and whether the apply actually killed.
  win probe     the same board with NO combo-off assertion and `max_turns == turn`: the autonomous
                engine plays the turn and reports its win turn.  This is the winnability oracle for
                classes (c) and (d) -- and it is an UPPER BOUND, because the turn engine steps into
                `turn` through an UNTAP and a DRAW that the offer probe (board as authored) never
                sees.  Every state records `untap_confound` (did it have tapped permanents) so a
                (c) verdict resting on the untap can be told apart from a real missed offer.

A third, independent check runs in PYTHON: `arith()` re-derives the loop's net mana per iteration,
the 60-iteration bank, and the cheapest finish cost from the card data in src/cards/data/cards.json
-- deliberately NOT by calling the engine, so it can disagree with the rule table.  That
disagreement is what class (d) is.

--- REPRODUCIBILITY ------------------------------------------------------------------------------

Every state carries a STABLE ID (`<family>-<sha1[:10]>` over the fixture's semantic content), so a
re-run after a fix diffs cleanly against logs/combo_off_sweep/results.json.  Nothing here depends on
wall clock, thread count or iteration order.

Usage:
    bash test/combo_off_sweep.sh                 # full sweep
    bash test/combo_off_sweep.sh --quick         # gate mode: synthetic core + 12 engine games
    python3 test/combo_off_sweep.py --help
"""
import argparse
import concurrent.futures as futures
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MTG = os.environ.get("MTG_BIN", os.path.join(ROOT, "build/Release/mtg"))
if not os.path.exists(MTG) and os.path.exists(MTG + ".exe"):
    MTG += ".exe"
DECK = "decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod"
DECK_ABS = os.path.join(ROOT, DECK)
REFDIR = os.path.join(ROOT, "references/EldraziDisplacerFlicker")
OUTDIR = os.path.join(ROOT, "logs/combo_off_sweep")

# ---------------------------------------------------------------------------------------------
# Card data.  Rule 0 of .claude/skills/claude-play.md: read the deck's cards from cards.json,
# never from recall.  Every number the python oracle uses below comes from here.
# ---------------------------------------------------------------------------------------------
_CARDS = None


def cards():
    global _CARDS
    if _CARDS is None:
        raw = json.load(open(os.path.join(ROOT, "src/cards/data/cards.json")))
        lst = raw["cards"] if isinstance(raw, dict) and "cards" in raw else raw
        _CARDS = {c["name"]: c for c in lst} if isinstance(lst, list) else lst
    return _CARDS


def par(name, key, default=None):
    c = cards().get(name) or {}
    return (c.get("parameters") or {}).get(key, default)


def mana_value(cost):
    """Mana value of a '{2}{C}' style string."""
    if not cost:
        return 0
    total = 0
    for tok in re.findall(r"\{([^}]*)\}", cost):
        if tok.isdigit():
            total += int(tok)
        else:
            total += 1
    return total


def pip_count(cost, sym):
    if not cost:
        return 0
    return sum(1 for t in re.findall(r"\{([^}]*)\}", cost) if t == sym)


def deck_main_and_side():
    """(main, side) as name -> count, straight off the .cod."""
    tree = ET.parse(DECK_ABS)
    main, side = {}, {}
    for zone in tree.getroot().iter("zone"):
        tgt = main if zone.get("name") == "main" else side
        for card in zone.iter("card"):
            tgt[card.get("name")] = tgt.get(card.get("name"), 0) + int(card.get("number"))
    return main, side


# ---------------------------------------------------------------------------------------------
# The python ORACLE.  An independent re-derivation of the arithmetic the rule table does in C++
# (comborules::BankableMana / CheapestFinishNeed / RecogniseFlickerLoop), written from the card
# data rather than from the engine, so the two can DISAGREE -- which is the whole point.
# ---------------------------------------------------------------------------------------------
FLICKER_MAX_ITERATIONS = 60      # DecisionProviders.cpp FlickerMaxIterations()
MAX_UNTAPS = 5                   # kFlickerMaxUntaps (Peregrine Drake is the ceiling)


def land_yield(name, auras):
    """Mana a single tap of this land produces, counting the Auras riding it."""
    base = par(name, "produces_amount", 1) or 1
    if not par(name, "produces"):
        return 0                                  # not a mana land at all
    return base + sum(par(a, "land_aura_extra_mana", 0) or 0 for a in auras)


def land_colors(name, auras, energy):
    """The colour SET a single tap can produce, Auras included."""
    out = set(par(name, "produces") or [])
    if name == "Aether Hub" and energy <= 0:
        out = {"C"}                               # the any-colour mode is energy-gated
    for a in auras:
        prod = par(a, "land_aura_produces", None)
        if prod:
            out |= set(prod)
        elif par(a, "is_land_aura"):
            out |= {"W", "U", "B", "R", "G"}      # "one mana of any color" -- never {C}
    return out


def effective_activation(cost, training_grounds, is_creature_ability):
    """Training Grounds: -{2} generic on a CREATURE's activated ability, floored at one mana, and
    the {C} pip survives (cards.json: 'the generic half goes and the colourless pip stays')."""
    if not cost:
        return 0, 0
    mv, cpips = mana_value(cost), pip_count(cost, "C")
    if training_grounds and is_creature_ability:
        mv = max(max(1, cpips), mv - 2)
    return mv, cpips


def arith(bf, hand, library, sideboard, opp_life, opp_lib, energy):
    """Independent arithmetic on a board.  `bf` is [(name, tapped, [aura names])].

    Returns a dict of the quantities every classification below rests on."""
    tg = any(n == "Training Grounds" for n, _, _ in bf)
    creatures = [n for n, _, _ in bf if par(n, "blink_cost") or par(n, "etb_untap_lands")
                 or par(n, "drain_cost") or par(n, "exile_opponent_top_cost")]

    # --- the loop -------------------------------------------------------------------------
    outlets = []
    for n, _, _ in bf:
        bc = par(n, "blink_cost")
        if bc:
            mv, cp = effective_activation(bc, tg, True)
            outlets.append((n, mv, cp, bool(par(n, "blink_own_only"))))
    untaps = max([par(n, "etb_untap_lands", 0) or 0 for n, _, _ in bf] + [0])
    untaps = min(untaps, MAX_UNTAPS)

    lands = [(n, t, a) for n, t, a in bf if par(n, "produces")]
    yields = sorted((land_yield(n, a) for n, _, a in lands), reverse=True)
    refund = sum(yields[:untaps])
    # {C} the untap can bring back: how many of the top-`untaps` lands can make {C} at all.  The
    # engine reserves one untap slot for a {C} land when the yield order would take none
    # (MTG_UNTAP_C_STARVED), so this counts the best case the same way the engine's does.
    c_lands = [n for n, _, a in lands if "C" in land_colors(n, a, energy)]
    ranked = sorted(lands, key=lambda la: -land_yield(la[0], la[2]))
    top = ranked[:untaps]
    c_in_top = sum(1 for n, _, a in top if "C" in land_colors(n, a, energy))
    if c_in_top == 0 and c_lands and untaps > 0:
        c_in_top = 1                              # the starved reservation

    best = None
    for name, mv, cp, _own in outlets:
        net = refund - mv
        net_c = c_in_top - cp
        cand = dict(outlet=name, cost=mv, cpip=cp, refund=refund, untaps=untaps,
                    net=net, net_c=net_c)
        if best is None or (net, net_c) > (best["net"], best["net_c"]):
            best = cand
    loop = best or dict(outlet=None, cost=0, cpip=0, refund=refund, untaps=untaps,
                        net=0, net_c=0)
    loop["ok"] = bool(outlets) and untaps > 0
    loop["bank"] = max(0, loop["net"]) * FLICKER_MAX_ITERATIONS if loop["ok"] else 0

    # --- what the board can PAY ------------------------------------------------------------
    producible = set()
    for n, _, a in lands:
        producible |= land_colors(n, a, energy)
    # A land Aura's wild bonus is this deck's only red (cards.json, Fertile Ground / Trace).
    has_red = "R" in producible
    nC = sum(1 for n, _, a in bf if "C" in land_colors(n, a, energy))
    has_ub = bool({"U", "B"} & producible)
    has_draw = any(par(n, "tap_draw_cost") or par(n, "tap_investigate_cost") for n, _, _ in bf)
    emiel = any(par(n, "blink_cost") and pip_count(par(n, "blink_cost"), "C") == 0
                for n, _, _ in bf)

    def can_cast(name):
        cost = (cards().get(name) or {}).get("mana_cost") or ""
        for sym in re.findall(r"\{([^}]*)\}", cost):
            if sym.isdigit():
                continue
            if sym == "C":
                if "C" not in producible:
                    return False
            elif "/" in sym:
                if not (set(sym.split("/")) & producible):
                    return False
            elif sym not in producible:
                return False
        return True

    def finisher_need(name, on_board):
        """Mana to RUN this finisher to a kill from here.  Mirrors comborules::FinishNeedMana."""
        if not on_board and not can_cast(name):
            return None
        need = 0 if on_board else mana_value((cards().get(name) or {}).get("mana_cost"))
        if par(name, "drain_cost") and (par(name, "drain_amount", 0) or 0) > 0:
            mv, _ = effective_activation(par(name, "drain_cost"), tg and on_board, True)
            acts = -(-max(1, opp_life) // par(name, "drain_amount"))
            return need + acts * max(1, mv)
        if par(name, "exile_opponent_top_cost"):
            if opp_lib <= 0:
                return None
            mv, _ = effective_activation(par(name, "exile_opponent_top_cost"), tg and on_board, True)
            return need + opp_lib * max(1, mv)
        return None

    def is_finisher(name):
        return bool((par(name, "drain_cost") and (par(name, "drain_amount", 0) or 0) > 0)
                    or par(name, "exile_opponent_top_cost"))

    needs = {}
    bf_needs = [finisher_need(n, True) for n, _, _ in bf if is_finisher(n)]
    needs["bf"] = min([x for x in bf_needs if x is not None], default=None)
    hand_needs = [finisher_need(n, False) for n in hand if is_finisher(n)]
    needs["hand"] = min([x for x in hand_needs if x is not None], default=None)
    wish_mv = None
    for n in hand:
        if par(n, "wish_from_sideboard"):
            wish_mv = mana_value((cards().get(n) or {}).get("mana_cost"))
            break
    wish_in_lib = any(par(n, "wish_from_sideboard") for n in library)
    if wish_mv is None and wish_in_lib and has_draw:
        for n in library:
            if par(n, "wish_from_sideboard"):
                wish_mv = mana_value((cards().get(n) or {}).get("mana_cost"))
                break
    if wish_mv is not None:
        w = [finisher_need(n, False) for n in sideboard if is_finisher(n)]
        w = [x + wish_mv for x in w if x is not None]
        needs["wish"] = min(w, default=None)
    else:
        needs["wish"] = None
    gorge = [n for n, _, _ in bf if par(n, "tap_damage_cost")]
    if gorge and has_red:
        each = par(gorge[0], "tap_damage_each_opponent", 0) or 0
        if each > 0:
            acts = -(-max(1, opp_life) // each)
            needs["gorge"] = acts * max(1, mana_value(par(gorge[0], "tap_damage_cost")))
            # A Gorge ping is once PER UNTAP: the loop must also untap it `acts` times, and the
            # untap is yield-ordered, so a low-yield Gorge outside the top-N is never restored.
            needs["gorge_untaps_needed"] = acts
            needs["gorge_in_top"] = any(n == gorge[0] for n, _, _ in top)
        else:
            needs["gorge"] = None
    else:
        needs["gorge"] = None

    live = [v for k, v in needs.items() if k in ("bf", "hand", "wish", "gorge") and v is not None]
    cheapest = min(live, default=None)
    return dict(loop=loop, nC=nC, has_ub=has_ub, has_red=has_red, has_draw=has_draw,
                emiel=emiel, needs=needs, cheapest_need=cheapest,
                producible=sorted(producible), training_grounds=tg,
                # PROVABLY UNWINNABLE, by the cheapest possible reading of every path -- the same
                # deliberate under-count the C++ rule uses, so a state flagged here is refuted with
                # the arithmetic's own thumb on the scale.
                unwinnable=(not loop["ok"]) or cheapest is None or loop["bank"] < cheapest,
                bank_vs_need=(loop["bank"], cheapest))


# ---------------------------------------------------------------------------------------------
# Fixture construction + the two probes.
# ---------------------------------------------------------------------------------------------
def make_fixture(bf, hand, turn, opp_lib, *, library=None, energy=0, opp_life=20, my_life=20,
                 lib_size=None, depth=3, budget_ms=20):
    """`bf` is [(name, tapped, [aura names])] -- Auras are emitted as separate `equips` entries
    AFTER their hosts, which is what the scenario loader's name resolution needs."""
    perms, aur = [], []
    for name, tapped, auras in bf:
        perms.append({"name": name, "controller": 0, "tapped": bool(tapped)})
        for a in auras:
            aur.append({"name": a, "controller": 0, "tapped": False, "equips": name})
    named_lib = list(library or [])
    if lib_size is None:
        lib_size = max(1, 40 - len(named_lib))
    fix = {
        "deck": DECK,
        "turn": turn,
        "on_the_play": False,
        "active_life": my_life,
        "opponent_life": opp_life,
        "opponent_library_size": opp_lib,
        "library_filler": "Forest",
        "library_size": lib_size,
        "depth": depth,
        "budget_ms": budget_ms,
        "battlefield": perms + aur,
        "hand": list(hand),
    }
    if energy:
        fix["energy_counters"] = energy
    if named_lib:
        # `library_top` is drawn FIRST, which is what a draw-engine go-off actually digs through.
        fix["library_top"] = named_lib
    return fix


def state_id(family, fix):
    """Stable, content-addressed, order-insensitive where order is not semantic."""
    sig = json.dumps({
        "bf": sorted((p["name"], p.get("tapped", False), p.get("equips", ""))
                     for p in fix["battlefield"]),
        "hand": sorted(fix["hand"]),
        "lib": fix.get("library_top", []),
        "turn": fix["turn"], "opp_lib": fix["opponent_library_size"],
        "opp_life": fix["opponent_life"], "energy": fix.get("energy_counters", 0),
    }, sort_keys=True)
    return "%s-%s" % (family, hashlib.sha1(sig.encode()).hexdigest()[:10])


OFFER_RE = re.compile(r"^scenario: combo_off plans=(\d+) offered=(\d) verified=(\d)"
                      r"(?: rule=(\S+))?(?: summary=\"(.*)\")?\s*$", re.M)
NOWIN_RE = re.compile(r"^scenario: FAIL combo_off plan applied but did NOT win"
                      r" \(opponent life (-?\d+), library (\d+)\)", re.M)
WIN_RE = re.compile(r"^scenario: win_turn=(\S+) opponent_life=(-?\d+) active_life=(-?\d+)", re.M)


def run_scenario(fix, env_extra=None):
    fd, path = tempfile.mkstemp(suffix=".json", dir=OUTDIR, text=True)
    with os.fdopen(fd, "w") as f:
        json.dump(fix, f)
    try:
        env = dict(os.environ)
        env.update(env_extra or {})
        p = subprocess.run([MTG, "--scenario", path], capture_output=True, text=True,
                           cwd=ROOT, env=env)
        return p.returncode, p.stdout + p.stderr
    finally:
        try:
            os.unlink(path)
        except OSError:
            pass


def probe_offer(fix, show_history=False, env_extra=None):
    """Rule table + the click.  `expect_combo_off: true` so an ABSENT button reports as a FAIL we
    parse rather than an exception; the flag is a question here, not an assertion."""
    f = dict(fix)
    f["env"] = {"MTG_HUMAN_PLAY": "1"}
    f["expect_combo_off"] = True
    f["combo_off_verify"] = True
    f["expect_history_no_macro"] = False          # not the question this sweep asks
    if show_history:
        f["combo_off_show_history"] = True
    rc, out = run_scenario(f, env_extra)
    m = OFFER_RE.search(out)
    res = {"offered": False, "verified": False, "rule": None, "plans": None,
           "summary": None, "apply_win": None, "opp_life_after": None,
           "opp_lib_after": None, "error": None}
    if not m:
        res["error"] = (out.strip().splitlines() or ["(no output)"])[-1][:200]
        return res, out
    res["plans"] = int(m.group(1))
    res["offered"] = m.group(2) == "1"
    res["verified"] = m.group(3) == "1"
    res["rule"] = m.group(4)
    res["summary"] = m.group(5)
    if res["offered"]:
        nw = NOWIN_RE.search(out)
        if nw:
            res["apply_win"] = False
            res["opp_life_after"] = int(nw.group(1))
            res["opp_lib_after"] = int(nw.group(2))
        elif "combo_off verified finish (opponent lost)" in out:
            res["apply_win"] = True
        else:
            res["apply_win"] = None
            res["error"] = "offered but neither win nor no-win line"
    return res, out


GOFF_RE = re.compile(r"^\[edf-goff\] t(\d+) src=(.*?)\(\d+\) tgt=(.*?)\(\d+\) ok=(\d) outlet=\d+ "
                     r"payload=\d+ net=(-?\d+) refund=(-?\d+) cost=(-?\d+) gorge=(-?\d+)/(-?\d+) "
                     r"drain=(-?\d+)/(-?\d+) exile=(-?\d+) kmax=(-?\d+) afford=(-?\d+) n=(-?\d+)",
                     re.M)
FINSTAT_RE = re.compile(r"^\[finish\] draw seen=(\d+) guard-refused=(\d+) paid=(\d+) \| "
                        r"finish calls=(\d+) hand=(\d+) wish=(\d+) no-mana=(\d+) "
                        r"none-found=(\d+) pay-fail=(\d+)", re.M)
HIST_RE = re.compile(r"^scenario:   hist \[(\w+)\] (.*)$", re.M)


def probe_trace(fix):
    """SECOND PASS, class (b)/(d) only: re-ask the same board with the engine's own instruments on.

    `MTG_EDF_GOFF_DEBUG` prints the recognizer's arithmetic AND the iteration count it sized
    (`n=`), and `MTG_EDF_FINISH_STATS` prints the counters Session 14b's diagnosis turned on --
    `none-found` vs `pay-fail` vs `no-mana` is the difference between "no finisher was a
    candidate", "the finisher was right there and its cast could not be paid", and "the mana ran
    out".  Diagnosis only: neither flag branches game logic."""
    f = dict(fix)
    f["env"] = {"MTG_HUMAN_PLAY": "1"}
    f["expect_combo_off"] = True
    f["expect_history_no_macro"] = False
    f["combo_off_show_history"] = True
    _rc, out = run_scenario(f, {"MTG_EDF_FINISH_STATS": "1", "MTG_EDF_GOFF_DEBUG": "1"})
    tr = {}
    g = GOFF_RE.search(out)
    if g:
        tr["goff"] = dict(outlet=g.group(2), payload=g.group(3), ok=g.group(4) == "1",
                          net=int(g.group(5)), refund=int(g.group(6)), cost=int(g.group(7)),
                          gorge_dmg=int(g.group(8)), gorge_cost=int(g.group(9)),
                          drain_amount=int(g.group(10)), drain_cost=int(g.group(11)),
                          exile_cost=int(g.group(12)), kmax=int(g.group(13)),
                          afford=int(g.group(14)), iterations=int(g.group(15)))
    fs = FINSTAT_RE.search(out)
    if fs:
        tr["finish"] = dict(draw_seen=int(fs.group(1)), draw_guard=int(fs.group(2)),
                            draw_paid=int(fs.group(3)), calls=int(fs.group(4)),
                            hand=int(fs.group(5)), wish=int(fs.group(6)),
                            no_mana=int(fs.group(7)), none_found=int(fs.group(8)),
                            pay_fail=int(fs.group(9)))
    hist = HIST_RE.findall(out)
    tally = {}
    for kind, text in hist:
        key = None
        if "blinked" in text:
            key = "blink"
        elif "loses" in text and "life" in text:
            key = "drain"
        elif "deals" in text and "opponent" in text:
            key = "ping"                          # Shivan Gorge
        elif "exiles their top card" in text:
            key = "exile"
        elif "combo finish" in text:
            key = "finish_cast"
        elif "draw" in text.lower() or "Clue" in text:
            key = "draw"
        elif kind == "cast":
            key = "cast"
        if key:
            tally[key] = tally.get(key, 0) + 1
    tr["events"] = tally
    tr["n_events"] = len(hist)
    # THE SINGLE MOST DIAGNOSTIC NUMBER IN THIS HARNESS: how many blinks the plan PROMISED against
    # how many the apply actually ran.  A promise of 31 and an execution of 0 is not "the finish
    # was short of mana", it is the loop never starting -- and those are different bugs.
    tr["blinks_run"] = tally.get("blink", 0)
    pm = re.search(r"summary=\".*?x(\d+)", out)
    tr["blinks_promised"] = int(pm.group(1)) if pm else None
    return tr


def probe_win(fix, env_extra=None):
    """Winnability oracle: the AUTONOMOUS engine plays exactly this turn.  UPPER BOUND -- it steps
    into `turn` through an untap and a draw the offer probe never sees."""
    f = dict(fix)
    f.pop("env", None)
    f["max_turns"] = f["turn"]
    f["depth"] = 5
    f["budget_ms"] = 100
    rc, out = run_scenario(f, env_extra)
    m = WIN_RE.search(out)
    if not m:
        return {"auto_win_turn": None, "error": (out.strip().splitlines() or ["?"])[-1][:200]}, out
    wt = m.group(1)
    return {"auto_win_turn": (None if wt == "none" else int(wt)),
            "auto_opp_life": int(m.group(2)), "error": None}, out


# ---------------------------------------------------------------------------------------------
# POPULATION 1 -- the synthetic matrix.
#
# Pruned to the combinations that CHANGE A RULE'S ANSWER.  Every family below is built around one
# ingredient of EldraziFlickerProvider::ComboOffPossible so a failure names its own mechanism.
# ---------------------------------------------------------------------------------------------
OUTLETS = {"displacer": "Eldrazi Displacer", "emiel": "Emiel the Blessed"}
UNTAPPERS = {"cloud": "Cloud of Faeries", "drake": "Peregrine Drake"}
# The deck's real mana lands, tagged by what they bring to a rule.
C_LANDS = ["Mariposa Military Base", "Adarkar Wastes", "Brushland", "Yavimaya Coast"]
UB_LANDS = ["Yavimaya Coast", "Adarkar Wastes", "Kitchen"]
DRAW_LANDS = {"mariposa": "Mariposa Military Base", "conservatory": "Conservatory",
              "kitchen": "Kitchen"}


def _bf(entries):
    """[(name, tapped, auras)] from a compact spec."""
    return [(e[0], e[1] if len(e) > 1 else False, list(e[2]) if len(e) > 2 else [])
            for e in entries]


def synthetic_states(quick=False):
    """Returns [(family, fixture, note)]."""
    out = []
    main, side = deck_main_and_side()
    sideboard = list(side)

    def add(family, bf, hand, note, *, turn=5, opp_lib=None, library=None, energy=0, opp_life=20):
        # A REALISTIC opponent library by default -- 53 minus one draw per turn elapsed, the same
        # estimate combo_off_frames.py uses.  The committed test/combo_off fixtures pin 12, which
        # makes the deck-out route ~4x cheaper than any real game's; sizing the sweep off that
        # would measure a bar the deck never actually has to clear.  Family A keeps a 12-card
        # variant so both ends are covered.
        if opp_lib is None:
            opp_lib = max(1, 53 - (turn - 1))
        fix = make_fixture(_bf(bf), hand, turn, opp_lib, library=library, energy=energy,
                           opp_life=opp_life)
        out.append((family, fix, note))

    # --- A: outlet x untapper x Training Grounds x finisher, on a RICH land base ---------------
    # The land base is deliberately fat (Kitchen+2 Overgrowth = 5, Conservatory+Wild Growth = 2,
    # two Mariposas = 2) so the loop is unambiguously live and the only thing under test is
    # whether the FINISH executes.
    rich = [("Kitchen", False, ["Overgrowth", "Overgrowth"]),
            ("Conservatory", False, ["Wild Growth"]),
            ("Mariposa Military Base",), ("Adarkar Wastes",), ("Yavimaya Coast",)]
    fin_variants = [
        ("fin-none", [], [], "no finisher anywhere"),
        ("fin-bf-depleter", [("Essence Depleter",)], [], "Essence Depleter already deployed"),
        ("fin-bf-infiltrator", [("Dimensional Infiltrator",)], [], "Infiltrator already deployed"),
        ("fin-hand-infiltrator", [], ["Dimensional Infiltrator"], "Infiltrator in hand"),
        ("fin-wish-hand", [], ["Living Wish"], "Living Wish in hand"),
        ("fin-wish-lib", [], [], "Living Wish in LIBRARY only (needs the draw engine)"),
    ]
    for okey, oname in OUTLETS.items():
        for ukey, uname in UNTAPPERS.items():
            for tg in (False, True):
                for fkey, extra_bf, extra_hand, note in fin_variants:
                    if quick and (tg or fkey in ("fin-bf-infiltrator", "fin-none")):
                        continue
                    for lkey, olib in (("lib49", None), ("lib12", 12)):
                        if quick and lkey == "lib12":
                            continue
                        bf = list(rich) + [(oname,), (uname,)] + list(extra_bf)
                        if tg:
                            bf.append(("Training Grounds",))
                        lib = ["Living Wish"] if fkey == "fin-wish-lib" else None
                        add("A_%s_%s%s_%s_%s" % (okey, ukey, "_tg" if tg else "", fkey, lkey),
                            bf, extra_hand,
                            "rich base; outlet=%s untapper=%s tg=%s; %s; opp_lib=%s"
                            % (oname, uname, tg, note, lkey),
                            library=lib, opp_lib=olib)

    # --- B: {C} STARVATION -- 0/1/2 colourless sources ----------------------------------------
    # The Displacer's {2}{C} needs a {C} pip PER ACTIVATION; Emiel's {3} needs none.  This is the
    # single sharpest divide in the deck and rule 2/3/5's `(D or E or C2)` rider encodes it.
    for okey, oname in OUTLETS.items():
        for ukey, uname in UNTAPPERS.items():
            for nc in (0, 1, 2):
                for fkey, hand, lib in (("wishhand", ["Living Wish"], None),
                                        ("bf", [], None)):
                    if quick and ukey == "drake" and nc == 2:
                        continue
                    bf = [("Kitchen", False, ["Overgrowth", "Overgrowth"]),
                          ("Conservatory", False, ["Wild Growth"]),
                          ("Conservatory",)]
                    bf += [(C_LANDS[i],) for i in range(nc)]
                    bf += [(oname,), (uname,)]
                    if fkey == "bf":
                        bf.append(("Dimensional Infiltrator",))
                    add("B_%s_%s_c%d_%s" % (okey, ukey, nc, fkey), bf, hand,
                        "{C} sources = %d; outlet=%s untapper=%s" % (nc, oname, uname),
                        library=lib)

    # --- C: THIN LOOP -- does the 60-iteration BANK cover the path? ----------------------------
    # Session 14c's seed-9 T4 board is the archetype: net +1 banks 60 mana against a ~104-mana
    # deck-out.  Walk the net from 0 upward and watch where the rule stops firing.
    thin_bases = [
        ("net0", [("Conservatory",), ("Mariposa Military Base",), ("Adarkar Wastes",)]),
        ("net1", [("Kitchen", False, ["Overgrowth"]), ("Mariposa Military Base",),
                  ("Conservatory",)]),
        ("net2", [("Kitchen", False, ["Overgrowth"]), ("Conservatory", False, ["Wild Growth"]),
                  ("Mariposa Military Base",)]),
        ("net4", [("Kitchen", False, ["Overgrowth", "Overgrowth"]),
                  ("Conservatory", False, ["Wild Growth"]), ("Mariposa Military Base",)]),
        ("net7", [("Kitchen", False, ["Overgrowth", "Overgrowth"]),
                  ("Conservatory", False, ["Overgrowth", "Wild Growth"]),
                  ("Mariposa Military Base",), ("Yavimaya Coast",)]),
    ]
    for nkey, base in thin_bases:
        for okey, oname in OUTLETS.items():
            for ukey, uname in UNTAPPERS.items():
                if quick and ukey == "drake":
                    continue
                for fkey, hand, extra in (("wishhand", ["Living Wish"], []),
                                          ("bfinf", [], [("Dimensional Infiltrator",)])):
                    bf = list(base) + [(oname,), (uname,)] + extra
                    add("C_%s_%s_%s_%s" % (nkey, okey, ukey, fkey), bf, hand,
                        "thin loop %s; outlet=%s untapper=%s" % (nkey, oname, uname), turn=4)

    # --- D: COLOUR -- can the board actually CAST the finisher it is being priced on? ----------
    # Essence Depleter is {2}{B}; the deck's only black is an Aether Hub holding energy.
    # Dimensional Infiltrator is {1}{U}.  FinishNeedMana applies BoardCanPayColors, so a board
    # with no black must not fire on the Depleter -- the seed-9 trap one rule over.
    colour_bases = [
        ("nob_nou", ["Brushland", "Brushland"]),                 # G/W/C only
        ("u_only", ["Yavimaya Coast", "Brushland"]),             # U present, no B
        ("hub_e0", ["Aether Hub", "Brushland"]),                 # B only with energy
        ("hub_e1", ["Aether Hub", "Brushland"]),                 # ... which this one has
    ]
    for ckey, extra_lands in colour_bases:
        for okey, oname in OUTLETS.items():
            for hkey, hand in (("wish", ["Living Wish"]), ("dep", ["Essence Depleter"]),
                               ("inf", ["Dimensional Infiltrator"])):
                bf = [("Kitchen", False, ["Overgrowth", "Overgrowth"]),
                      ("Conservatory", False, ["Wild Growth"]),
                      ("Mariposa Military Base",)]
                bf += [(n,) for n in extra_lands]
                bf += [(oname,), ("Peregrine Drake",)]
                add("D_%s_%s_%s" % (ckey, okey, hkey), bf, hand,
                    "colour probe %s; hand=%s" % (ckey, hand),
                    energy=1 if ckey == "hub_e1" else 0)

    # --- E: SHIVAN GORGE -- rule 1, the only path with no {C} requirement at all ---------------
    # USER: "only accessible with red mana on board (fertile ground or trace of abundance) and
    # Shivan Gorge already out + infinite mana."  The ping is once PER UNTAP and the Gorge's own
    # yield is 1, so whether the yield-ordered untap ever restores it is a live execution question
    # the rule does not ask.
    for red in ("none", "fertile", "trace"):
        for okey, oname in OUTLETS.items():
            for ukey, uname in UNTAPPERS.items():
                for opp_life in (20, 5):
                    if quick and (opp_life == 20 or ukey == "cloud"):
                        continue
                    aura = {"none": [], "fertile": ["Fertile Ground"],
                            "trace": ["Trace of Abundance"]}[red]
                    bf = [("Kitchen", False, ["Overgrowth", "Overgrowth"]),
                          ("Conservatory", False, ["Wild Growth"]),
                          ("Brushland", False, aura),
                          ("Shivan Gorge",), ("Mariposa Military Base",),
                          (oname,), (uname,)]
                    add("E_gorge_%s_%s_%s_l%d" % (red, okey, ukey, opp_life), bf, [],
                        "Shivan Gorge, red=%s, opp life %d" % (red, opp_life),
                        opp_life=opp_life)

    # --- F: DRAW SOURCE -- rule 4 (WISH-DRAW) turns entirely on D ------------------------------
    # With the wish still in the LIBRARY, W is live only when a repeatable draw source is in play
    # (comborules::WishReachesFinisher).  This family is where the seed-6 shape lives: the loop
    # has to dig to the wish and then still cast it.
    for dkey, dland in [("none", None)] + list(DRAW_LANDS.items()):
        for okey, oname in OUTLETS.items():
            for wkey, hand, lib in (("wishlib", [], ["Living Wish"]),
                                    ("wishhand", ["Living Wish"], None)):
                if quick and wkey == "wishhand":
                    continue
                bf = [("Kitchen", False, ["Overgrowth", "Overgrowth"]),
                      ("Conservatory", False, ["Wild Growth"]),
                      ("Mariposa Military Base",), ("Yavimaya Coast",)]
                if dland and dland not in [b[0] for b in bf]:
                    bf.append((dland,))
                bf += [(oname,), ("Peregrine Drake",)]
                # Bury the wish under real deck cards so the dig has to be a real dig.
                lib_full = None
                if lib:
                    lib_full = ["Wild Growth", "Brushland", "Fertile Ground", "Overgrowth",
                                "Conservatory", "Trace of Abundance", "Brushland",
                                "Yavimaya Coast", "Wild Growth", "Fertile Ground",
                                "Overgrowth", "Adarkar Wastes"] + lib
                add("F_draw_%s_%s_%s" % (dkey, okey, wkey), bf, hand,
                    "draw source=%s; wish %s" % (dkey, wkey), library=lib_full)

    # --- G: AETHER HUB energy -- the one conditional producer in the deck ----------------------
    for e in (0, 1, 2):
        for okey, oname in OUTLETS.items():
            bf = [("Kitchen", False, ["Overgrowth", "Overgrowth"]),
                  ("Conservatory", False, ["Wild Growth"]),
                  ("Aether Hub",), ("Brushland",),
                  (oname,), ("Peregrine Drake",)]
            add("G_hub_e%d_%s" % (e, okey), bf, ["Living Wish"],
                "Aether Hub with %d energy" % e, energy=e)

    # --- H: TAPPED {C} SOURCE -- the MTG_UNTAP_C_STARVED reservation ---------------------------
    # The seed-9 gi=8 frame: the yield order takes the two fat lands and the board's only {C}
    # source, tapped, never comes back.  Both untappers, both outlets, so the reservation's
    # bounded shape is exercised on the board it was written for.
    for okey, oname in OUTLETS.items():
        for ukey, uname in UNTAPPERS.items():
            for tapkey, tapped in (("untapped", False), ("tapped", True)):
                bf = [("Kitchen", False, ["Overgrowth", "Overgrowth"]),
                      ("Conservatory", False, ["Wild Growth"]),
                      ("Conservatory",),
                      ("Mariposa Military Base", tapped),
                      (oname,), (uname,)]
                add("H_conly_%s_%s_%s" % (tapkey, okey, ukey), bf, ["Living Wish"],
                    "sole {C} source %s; outlet=%s untapper=%s" % (tapkey, oname, uname))
    return out


# ---------------------------------------------------------------------------------------------
# POPULATION 2 -- the user's saved reference games (READ-ONLY; never written, never reverted).
# ---------------------------------------------------------------------------------------------
def reference_states(limit=None):
    out = []
    if not os.path.isdir(REFDIR):
        return out
    for path in sorted(os.listdir(REFDIR)):
        if not path.endswith(".json"):
            continue
        try:
            d = json.load(open(os.path.join(REFDIR, path)))
        except Exception:
            continue
        stem = os.path.splitext(path)[0]
        for i, ent in enumerate(d.get("decisions", [])):
            dec = ent.get("decision") or {}
            if dec.get("type") != "main_phase" or "me" not in dec:
                continue
            fix = fixture_from_snapshot(dec)
            if fix is None:
                continue
            out.append(("R_%s_%d" % (stem, i), fix,
                        "reference %s decision %d (turn %s)" % (stem, i, dec.get("turn"))))
            if limit and len(out) >= limit:
                return out
    return out


def fixture_from_snapshot(dec):
    """A saved --claude-play decision frame -> a scenario fixture.

    Same reconstruction combo_off_frames.py does, with the SAME documented lower-bound caveats --
    floating mana cannot be staged, so a frame the user reached with {G:86} banked is re-asked with
    an empty pool -- plus one IMPROVEMENT it does not have: the LIBRARY CONTENTS are rebuilt from
    the decklist minus (hand + battlefield + graveyard).  combo_off_frames.py fills the library
    with Forests, under which a Living Wish still in the deck is invisible and rule WISH-DRAW can
    never be asked at all on the population it matters most for.

    The reconstruction is a multiset, not an order, and it cannot see EXILE (a resolved Living Wish
    exiles itself, CR 400.11b / cards.json `exiles_self_on_resolve`).  So it can over-count.  When
    the rebuilt count exceeds the frame's recorded `library_size` the fixture is flagged
    `_lib_approx`, and any FALSE-FIRE conclusion drawn on such a state is withheld."""
    me = dec.get("me") or {}
    opp = dec.get("opponent") or {}
    turn = dec.get("turn", 4)
    bfl = me.get("battlefield") or []
    by_num = {c.get("num"): c.get("name") for c in bfl}
    hosts = {}
    perms = []
    for c in bfl:
        host = c.get("attached_to")
        if host is not None and host in by_num:
            hosts.setdefault(host, []).append(c["name"])
    for c in bfl:
        if c.get("attached_to") in by_num:
            continue
        perms.append((c["name"], bool(c.get("tapped")), hosts.get(c.get("num"), [])))
    hand = [c["name"] for c in (me.get("hand") or []) if c.get("name")]
    gy = [c.get("name") for c in (me.get("graveyard") or []) if isinstance(c, dict)]
    main, _side = deck_main_and_side()
    left = dict(main)
    for n in hand + gy + [p[0] for p in perms] + [a for p in perms for a in p[2]]:
        if left.get(n):
            left[n] -= 1
    lib = []
    for n in sorted(left):
        lib += [n] * max(0, left[n])
    recorded = max(1, me.get("library_size", 40))
    approx = len(lib) > recorded
    fix = make_fixture(perms, hand, turn, max(1, 53 - (turn - 1)),
                       library=lib,
                       opp_life=opp.get("life", 20), my_life=me.get("life", 20),
                       lib_size=max(0, recorded - len(lib)))
    if approx:
        fix["_lib_approx"] = True
    return fix


# ---------------------------------------------------------------------------------------------
# POPULATION 3 -- AUTONOMOUS engine play at the deck's shipped settings.
#
# The population the SEARCH-SHORTCUT question is about.  No --depth / --budget-ms, so the deck's
# own value_play policy (or the built-in default) governs -- i.e. exactly the states the search
# would be asked about if the rule table were ever wired into it.
# ---------------------------------------------------------------------------------------------
def run_engine_games(games, seed, threads, gamedir, reuse=False):
    # ONE pooled invocation for every game, never a loop of per-seed runs (CLAUDE.md's pooled-queue
    # rule).  This is the sweep's only barrier and it is a genuine data dependency: the mined states
    # do not exist until the games have been played.
    if reuse and os.path.isdir(gamedir) and os.listdir(gamedir):
        return 0, "(reused %d existing game logs)" % len(os.listdir(gamedir))
    if os.path.isdir(gamedir):
        shutil.rmtree(gamedir)
    os.makedirs(gamedir, exist_ok=True)
    args = [MTG, DECK, "--games", str(games), "--seed", str(seed), "--max-turns", "8",
            "--log-dir", gamedir, "--threads", str(threads)]
    p = subprocess.run(args, capture_output=True, text=True, cwd=ROOT)
    return p.returncode, (p.stdout + p.stderr)[-2000:]


def engine_states(gamedir, limit_per_game=None):
    """Every MAIN-phase decision point of every logged game.

    The board at the START of MAIN_1 is `boardAfter` of that turn's DRAW phase; the board at the
    start of MAIN_2 is `boardAfter` of COMBAT.  Library CONTENTS are reconstructed as decklist
    minus (hand + battlefield + graveyard + everything already cast and gone), which is what makes
    the WISH-DRAW rule's library scan answerable at all -- a Forest-filler reconstruction can never
    see a Living Wish still in the deck.  Library ORDER is NOT recoverable from the log and is
    therefore canonical (decklist order); this is stated in the catalogue as a fidelity limit."""
    out = []
    main, side = deck_main_and_side()
    for fn in sorted(os.listdir(gamedir)):
        if not fn.endswith(".json"):
            continue
        try:
            g = json.load(open(os.path.join(gamedir, fn)))
        except Exception:
            continue
        num2name = {}
        for name, nums in (g.get("cardNumbering") or {}).items():
            for n in nums:
                num2name[n] = name
        win_turn = ((g.get("result") or {}).get("turn")) or None
        phases = g.get("turns") or []
        # Which main-phase decisions come AFTER a given turn -- the work a shortcut would save.
        mains_at = [p.get("turn") for p in phases if p.get("phase") in ("MAIN_1", "MAIN_2")]
        cast_gone = set()
        n_in_game = 0
        for pi, pe in enumerate(phases):
            for a in pe.get("actions") or []:
                if a.get("type") == "CAST_SPELL":
                    cast_gone.add(a.get("card"))
            nxt = phases[pi + 1] if pi + 1 < len(phases) else None
            if nxt is None:
                continue
            if not (pe.get("phase") in ("DRAW", "COMBAT")
                    and nxt.get("phase") in ("MAIN_1", "MAIN_2")
                    and nxt.get("turn") == pe.get("turn")):
                continue
            board = pe.get("boardAfter") or {}
            bfl = board.get("battlefield") or []
            hosts, perms, on_bf = {}, [], set()
            for c in bfl:
                if c.get("attachedTo"):
                    hosts.setdefault(c["attachedTo"], []).append(c["cardName"])
            for c in bfl:
                on_bf.add(c.get("card"))
                if c.get("attachedTo"):
                    continue
                perms.append((c["cardName"], bool(c.get("tapped")), hosts.get(c.get("card"), [])))
            hand_nums = board.get("hand") or []
            hand = [num2name.get(n, "") for n in hand_nums]
            hand = [h for h in hand if h]
            gy = set(board.get("graveyard") or [])
            seen = set(hand_nums) | on_bf | gy | (cast_gone - on_bf)
            lib = [num2name[n] for n in sorted(num2name) if n not in seen and n in num2name]
            turn = pe.get("turn")
            fix = make_fixture(perms, hand, turn, max(1, 53 - (turn - 1)),
                               library=lib,
                               opp_life=board.get("opponentLife", 20),
                               my_life=board.get("playerLife", 20),
                               lib_size=1)
            after = sum(1 for t in mains_at if t is not None and t > turn)
            out.append(("X_%s_t%d_%s" % (os.path.splitext(fn)[0], turn, nxt.get("phase")), fix,
                        json.dumps({"game": fn, "turn": turn, "phase": nxt.get("phase"),
                                    "win_turn": win_turn, "mains_after": after})))
            n_in_game += 1
            if limit_per_game and n_in_game >= limit_per_game:
                break
    return out


# ---------------------------------------------------------------------------------------------
# Classification.
# ---------------------------------------------------------------------------------------------
def mechanism(rec):
    """Cluster a class-(b) EXECUTOR FAILURE by what the evidence says stopped it.

    Every tag names ONE repairable thing and is drawn from the engine's own instruments wherever
    one exists -- the `[edf-goff]` recognizer line, the `[finish]` counters, and the apply's event
    stream -- rather than from inference over the board.  A state can carry several."""
    o, a, tr = rec["offer"], rec["arith"], rec.get("trace") or {}
    goff, fin, ev = tr.get("goff") or {}, tr.get("finish") or {}, tr.get("events") or {}
    tags = []
    rule = o.get("rule") or ""
    lib_after, life_after = o.get("opp_lib_after"), o.get("opp_life_after")
    loop = a["loop"]

    # --- 1. THE FINISHER NEVER GOT A CARD INTO PLAY -------------------------------------------
    if fin.get("pay_fail", 0) > 0:
        # Session 14b's shape exactly: the card is in hand and its cast cannot be paid.
        tags.append("finisher-cast-unpayable")
    if fin.get("no_mana", 0) > 0:
        tags.append("finisher-no-mana")
    if rule.startswith("WISH") and fin.get("wish", 0) == 0 and fin.get("calls", 0) > 0:
        tags.append("wish-never-cast")
    if rule.startswith("WISH") and rec["wish_in_lib_only"]:
        # The seed-6 shape: the wish is still in the LIBRARY, so the loop has to dig to it and
        # then still cast it.  Tagged whenever the wish route was the offer's own justification.
        tags.append("wish-in-library-dig")
        if fin.get("draw_seen", 0) == 0:
            tags.append("draw-loop-never-ran")
        elif fin.get("wish", 0) == 0:
            tags.append("draw-loop-stopped-early")

    # --- 2. THE COLOURLESS ECONOMY ------------------------------------------------------------
    # The decisive quantity for any {C}-pip finisher is the loop's colourless NET PER ITERATION,
    # not the board's {C} source COUNT -- and `ComboOffPossible` reads only the count (C1/C2 via
    # comborules::ColorlessSourceCount).  A Displacer loop spends one {C} a pass; if the untap
    # restores only one, the sink is fed at net zero however many sources are on the board.
    needs_c_pip = any(pip_count(par(n, "drain_cost") or par(n, "exile_opponent_top_cost") or "",
                                "C") > 0
                      for n in ([p["name"] for p in rec["fixture"]["battlefield"]]
                                + list(rec["fixture"]["hand"])))
    if needs_c_pip and loop["net_c"] <= 0 and loop["ok"]:
        tags.append("c-net-nonpositive" if loop["net_c"] == 0 else "c-net-negative")
    if a["nC"] == 0 and not a["emiel"]:
        tags.append("c-starved-no-source")

    # --- 3. DID THE LOOP RUN AT ALL? ----------------------------------------------------------
    # ApplyBlinkLoop breaks on the FIRST iteration it cannot pay, and two independent things spend
    # the mana the blink was about to need -- both BEFORE `pay(c)` in the same iteration body:
    #   * SpendSurplusOnDamageSinks (a Shivan Gorge on the battlefield), and
    #   * SpendSurplusOnDrawSinks   (the draw-to-find route, live exactly when no finisher is yet
    #     reachable -- i.e. whenever the Living Wish is still in the library).
    # Both are guarded by a CanPay PROJECTION over `AddManaCosts(sink_cost, blink_cost)`, and the
    # projection passes on boards where the real payment then does not leave the blink payable.
    run_, promised = tr.get("blinks_run"), tr.get("blinks_promised")
    if promised is not None and run_ is not None and promised > 1:
        if run_ == 0:
            tags.append("loop-zero-iterations")
            damage_sink = any(par(p["name"], "tap_damage_cost")
                              for p in rec["fixture"]["battlefield"])
            if damage_sink:
                tags.append("damage-sink-starves-loop")
            if not rec["finisher_reachable"]:
                tags.append("draw-to-find-starves-loop")
        elif run_ < promised:
            tags.append("loop-stopped-early")
    it = goff.get("iterations")
    if it is not None and it >= FLICKER_MAX_ITERATIONS:
        tags.append("iteration-cap-60")
    if loop["ok"] and loop["bank"] < (a["cheapest_need"] or 10 ** 9):
        tags.append("bank-short")                 # the rule fired on a loop that cannot pay

    # --- 4. THE GORGE PATH --------------------------------------------------------------------
    if rule == "GORGE":
        if not a["needs"].get("gorge_in_top", True):
            tags.append("gorge-not-untapped")     # once per untap, and the untap is yield-ordered
        if a["needs"].get("gorge_untaps_needed", 0) > FLICKER_MAX_ITERATIONS:
            tags.append("gorge-iterations")
        if not a["has_red"]:
            tags.append("gorge-no-red")

    # --- 5. WHAT THE BOARD ACTUALLY DID -------------------------------------------------------
    if lib_after == 0 and (life_after or 0) > 0:
        tags.append("decked-out-but-alive")       # library gone and the win never registered
    elif ev.get("drain") or ev.get("exile") or ev.get("ping"):
        tags.append("finish-fired-short")         # it fired and ran out
    elif ev.get("blink"):
        tags.append("loop-ran-finish-never-fired")
    elif not ev:
        tags.append("apply-did-nothing")

    if not tags:
        tags.append("other")
    # Stable order so a re-run diffs cleanly.
    return sorted(set(tags))


def classify(rec):
    o = rec["offer"]
    a = rec["arith"]
    if o.get("error"):
        return "x_error"
    if o["offered"]:
        if o["apply_win"]:
            return "a_offered_wins"
        if a["unwinnable"]:
            return "d_rule_too_loose"
        return "b_executor_failure"
    # --- not offered.  The winnability oracle is the autonomous engine, and it counts ANY win --
    # including a plain COMBAT kill, which is not what the Combo Off button is for.  A board with
    # five power facing five life "wins this turn" without a combo in sight, so crediting that as
    # a missed OFFER would manufacture class (c) out of ordinary attacking.  Split it off rather
    # than dropping it: a combat-lethal board is still worth knowing about, it is just not a rule
    # defect.
    if rec["win"].get("auto_win_turn") is not None:
        return "c_missed_offer_combat" if rec["combat_lethal"] else "c_missed_offer"
    return "e_absent_unwinnable"


# ---------------------------------------------------------------------------------------------
# Driver.
# ---------------------------------------------------------------------------------------------
def analyse_one(item):
    family, fix, note = item
    sid = state_id(family.split("_")[0], fix)
    meta = None
    if isinstance(note, str) and note.startswith("{"):
        try:
            meta = json.loads(note)
        except ValueError:
            meta = None
    bf = []
    hosts = {}
    for p in fix["battlefield"]:
        if p.get("equips"):
            hosts.setdefault(p["equips"], []).append(p["name"])
    for p in fix["battlefield"]:
        if p.get("equips"):
            continue
        bf.append((p["name"], p.get("tapped", False), hosts.get(p["name"], [])))
    _main, side = deck_main_and_side()
    library = fix.get("library_top", [])
    a = arith(bf, fix["hand"], library, list(side), fix["opponent_life"],
              fix["opponent_library_size"], fix.get("energy_counters", 0))
    offer, offer_out = probe_offer(fix)

    def is_fin(n):
        return bool((par(n, "drain_cost") and (par(n, "drain_amount", 0) or 0) > 0)
                    or par(n, "exile_opponent_top_cost"))
    # Total power that could attack this turn, for the combat-lethal split in classify().  `sick`
    # is never set by any generator here, so every creature on an authored board can attack.
    power = 0
    for name, _tapped, _auras in bf:
        c = cards().get(name) or {}
        try:
            power += int(c.get("power") or 0)
        except (TypeError, ValueError):
            pass
    rec = {
        "id": sid, "family": family, "note": note,
        "turn": fix["turn"], "offer": offer, "arith": a,
        "untap_confound": any(p.get("tapped") for p in fix["battlefield"]),
        "wish_in_hand": any(par(h, "wish_from_sideboard") for h in fix["hand"]),
        "wish_in_lib_only": (not any(par(h, "wish_from_sideboard") for h in fix["hand"])
                             and any(par(l, "wish_from_sideboard") for l in library)),
        # Is a finisher reachable WITHOUT digging?  This is the predicate ApplyBlinkLoop's
        # `want_draw` / draw-land promotion turns on (ComboFinisherReachable), and it is the one
        # that decides whether the draw-to-find route runs inside the apply at all.
        "finisher_reachable": (any(is_fin(n) for n, _, _ in bf)
                               or any(is_fin(h) for h in fix["hand"])
                               or any(par(h, "wish_from_sideboard") for h in fix["hand"])),
        "board_power": power,
        "combat_lethal": power >= fix["opponent_life"],
        "meta": meta,
        "win": {},
    }
    # The winnability oracle costs a second full turn of search, so run it only where the answer
    # can move a class: an absent button (is this a MISSED OFFER?) or a click that did not win
    # (is the position winnable at all, or is the rule simply too loose?).
    if (not offer["offered"]) or offer["apply_win"] is False:
        rec["win"], _ = probe_win(fix)
    rec["fixture"] = fix
    rec["class"] = classify(rec)
    if rec["class"] in ("b_executor_failure", "d_rule_too_loose"):
        rec["trace"] = probe_trace(fix)
        rec["mechanism"] = mechanism(rec)
    return rec


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--quick", action="store_true",
                    help="gate mode: the synthetic core + 12 engine games")
    ap.add_argument("--games", type=int, default=60, help="autonomous games to mine")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--workers", type=int, default=12)
    ap.add_argument("--skip", default="", help="comma list of populations to skip "
                                               "(synthetic,reference,engine)")
    ap.add_argument("--reuse-games", action="store_true",
                    help="mine the game logs already under logs/combo_off_sweep/games instead of "
                         "replaying them -- the re-run path after a fix, so the two sweeps are "
                         "compared on the SAME states")
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    if not os.path.exists(MTG):
        print("ERROR: %s not found -- build Release first (./build.sh)." % MTG, file=sys.stderr)
        return 2
    os.makedirs(OUTDIR, exist_ok=True)
    skip = {s.strip() for s in args.skip.split(",") if s.strip()}
    games = 12 if args.quick else args.games

    items = []
    if "synthetic" not in skip:
        items += synthetic_states(quick=args.quick)
    if "reference" not in skip and not args.quick:
        items += reference_states()
    if "engine" not in skip:
        gamedir = os.path.join(OUTDIR, "games")
        rc, tail = run_engine_games(games, args.seed, min(args.workers, 12), gamedir,
                                    reuse=args.reuse_games)
        if rc != 0:
            print("WARNING: engine games returned %d:\n%s" % (rc, tail), file=sys.stderr)
        if os.path.isdir(gamedir):
            items += engine_states(gamedir)

    # DEDUPE, then ONE pooled queue over every probe of every population -- no per-family waves,
    # so the box drains to a single tail (CLAUDE.md: "WAVES ARE A LOOP").
    seen, uniq = set(), []
    for family, fix, note in items:
        sid = state_id(family.split("_")[0], fix)
        if sid in seen:
            continue
        seen.add(sid)
        uniq.append((family, fix, note))
    print("states: %d unique (%d before dedupe), %d workers"
          % (len(uniq), len(items), args.workers))

    results = []
    with futures.ThreadPoolExecutor(max_workers=args.workers) as ex:
        futs = {ex.submit(analyse_one, it): it for it in uniq}
        done = 0
        for f in futures.as_completed(futs):
            done += 1
            try:
                results.append(f.result())
            except Exception as e:                # a bad fixture must not sink the sweep
                fam = futs[f][0]
                results.append({"id": "ERR-" + fam, "family": fam, "class": "x_error",
                                "error": repr(e)[:300]})
            if done % 50 == 0:
                print("  ... %d/%d" % (done, len(uniq)), flush=True)

    results.sort(key=lambda r: (r.get("class", ""), r.get("family", "")))
    out = args.out or os.path.join(OUTDIR, "results.json")
    with open(out, "w") as f:
        json.dump({"binary": MTG, "games": games, "seed": args.seed,
                   "states": len(results), "results": results}, f, indent=1, sort_keys=True)

    counts = {}
    for r in results:
        counts[r.get("class", "?")] = counts.get(r.get("class", "?"), 0) + 1
    print("\n=== COMBO OFF SWEEP: %d states ===" % len(results))
    for k in sorted(counts):
        print("  %-24s %5d" % (k, counts[k]))
    mech = {}
    for r in results:
        for m in r.get("mechanism", []):
            mech[m] = mech.get(m, 0) + 1
    if mech:
        print("  -- executor-failure mechanisms --")
        for k in sorted(mech, key=lambda x: -mech[x]):
            print("     %-26s %4d" % (k, mech[k]))
    rules = {}
    for r in results:
        rl = (r.get("offer") or {}).get("rule")
        if rl:
            rules[rl] = rules.get(rl, 0) + 1
    if rules:
        print("  -- rules fired --")
        for k in sorted(rules, key=lambda x: -rules[x]):
            print("     %-14s %4d" % (k, rules[k]))

    # --- per-population split, and THE SEARCH-SHORTCUT NUMBERS --------------------------------
    # The rule table is intended to become a shortcut the SEARCH can take: when the rules fire and
    # the finish verifies, stop enumerating the rest of a go-off turn and take the win.  For that
    # the population that matters is autonomous play at the deck's shipped settings, and the two
    # numbers that matter are the error rates in each direction:
    #   FALSE FIRE  offered on a state the arithmetic refutes -- inside the search this would
    #               corrupt evaluation and ground truth, so it is the dangerous direction;
    #   MISSED FIRE not offered on a state the engine wins that turn anyway -- this merely wastes
    #               the shortcut.
    pops = {}
    for r in results:
        p = {"X": "engine", "R": "reference"}.get(r.get("family", "?")[0], "synthetic")
        pops.setdefault(p, []).append(r)
    print("\n  -- by population --")
    for p in sorted(pops):
        cc = {}
        for r in pops[p]:
            cc[r.get("class", "?")] = cc.get(r.get("class", "?"), 0) + 1
        print("     %-10s n=%-5d %s" % (p, len(pops[p]),
                                        " ".join("%s=%d" % (k[0], v) for k, v in sorted(cc.items()))))

    eng = pops.get("engine", [])
    if eng:
        offered = [r for r in eng if (r.get("offer") or {}).get("offered")]
        verified = [r for r in offered if (r.get("offer") or {}).get("verified")]
        false_fire = [r for r in offered if r.get("class") == "d_rule_too_loose"]
        missed = [r for r in eng if r.get("class") == "c_missed_offer"]
        # First firing state per game, and the work a shortcut would have saved from there.
        first = {}
        for r in sorted(eng, key=lambda x: (x["meta"]["game"], x["meta"]["turn"])):
            if not (r.get("offer") or {}).get("offered"):
                continue
            g = r["meta"]["game"]
            if g not in first:
                first[g] = r
        games = {r["meta"]["game"] for r in eng}
        saved = [r["meta"]["mains_after"] for r in first.values()]
        turns_saved = [((r["meta"]["win_turn"] or 9) - r["meta"]["turn"]) for r in first.values()]
        print("\n  -- SEARCH-SHORTCUT VIEW (autonomous play, deck's shipped settings) --")
        print("     states %d over %d games; offered %d (%.1f%%), of those verified %d (%.1f%%)"
              % (len(eng), len(games), len(offered), 100.0 * len(offered) / max(1, len(eng)),
                 len(verified), 100.0 * len(verified) / max(1, len(offered))))
        print("     FALSE FIRE  (offered, arithmetic refutes): %d  (%.2f%% of states)"
              % (len(false_fire), 100.0 * len(false_fire) / max(1, len(eng))))
        print("     MISSED FIRE (absent, engine wins that turn): %d  (%.2f%% of states)"
              % (len(missed), 100.0 * len(missed) / max(1, len(eng))))
        print("     games with any fire: %d/%d" % (len(first), len(games)))
        if saved:
            print("     at FIRST fire: mean %.2f further main-phase decisions and %.2f further "
                  "turns to the engine's own win" % (sum(saved) / len(saved),
                                                     sum(turns_saved) / len(turns_saved)))
    print("\nwrote %s" % out)
    # A sweep is a REPORT, like combo_off_frames.py.  Only a harness error is a nonzero exit;
    # class (b)/(c)/(d) counts are the finding, and gating on them belongs to phase 2.
    return 1 if counts.get("x_error") else 0


if __name__ == "__main__":
    sys.exit(main())

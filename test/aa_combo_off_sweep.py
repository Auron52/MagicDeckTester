#!/usr/bin/env python3
"""COMBO OFF SWEEP for AccomplishedAlchemist -- does the button fire where it should, does the click WIN?

USER, 2026-10-20: *"We should use the combo off logic for this deck. It is very much the type of deck that
needs it."*  The doctrine is the EldraziDisplacerFlicker one (test/combo_off_sweep.py, docs/design/
combo-off-sweep-catalogue.md): DISPLAY must be AGGRESSIVE (fire wherever the rule holds) and EXECUTION
must be EXACT (a click wins).  This harness asks both, over two populations, and sorts every state into

    a   offered + the click WINS                         -- good
    b   offered + the click does NOT win, though the autonomous engine wins this turn  -- EXECUTOR FAILURE
    c   not offered + the autonomous engine wins this turn by a COMBO (Stroke deck-out / drain)
                                                         -- MISSED OFFER (rule too tight)
    c'  not offered + the win is COMBAT damage (Ezuri's pump, the Witch's +3/+3, plain attackers)
                                                         -- not what the button is for (main-phase kills only)
    d   offered + NOTHING wins this turn, not even the autonomous engine at 10x budget
                                                         -- RULE TOO LOOSE (report; never silently narrow)
    e   not offered + nothing wins                        -- good

POPULATIONS
  synthetic  a matrix over the rule table's own ingredients: LIFE (none / Nourish / Feed the Clan /
             Weather the Storm / already gained) x LOOP (Umbra on board / in hand, Staff on board / in
             hand, Witch on board / in hand, Galvanic in hand, none) x FINISH (Stroke / Living Wish /
             Scholar in hand / none) x mana (lands 2..6, an Island or not) x Alchemist (ready / sick).
  engine     every MAIN_1 frame of autonomous games (one pooled `--games N --threads T` run; the game
             logs' `boardAfter` of the DRAW phase), library rebuilt as decklist minus everything seen.

HOW A STATE IS ASKED: a `--scenario` fixture with `expect_combo_off` under MTG_HUMAN_PLAY=1 (the viewer's
own entry point, then the flagged plan re-applied through ApplyPlan exactly as a click), and -- where the
answer can move the class -- a WIN probe: the same board with max_turns == turn, autonomous, d5 b100, then
(for a would-be d) d5 b1000, resumed at MAIN 1 of the authored board (`resume_at`: no untap, no draw) so the
two probes see the same position.

Every combo kill in this deck lands in MAIN 1 (the opponent decked or drained to 0), so the WIN probe's
opponent library / life at the end tells a combo win from a combat one: decked or `opponent_life <= 0`
with the opponent library < its start = Stroke; life 0 with no attacker able to deal it = drain.  The
split is approximate and stated as such: `win_kind` = "combo" if the library shrank or the board had no
attackers, else "combat".

This is a REPORT, not a gate: it exits nonzero only on a harness error.

Usage:
    python3 test/aa_combo_off_sweep.py                    # synthetic + 40 engine games
    python3 test/aa_combo_off_sweep.py --quick            # synthetic core + 10 games
    python3 test/aa_combo_off_sweep.py --games 120 --reuse-games
Results: logs/aa_combo_off_sweep/results.json (keyed by a stable content-addressed state id).
"""
import argparse
import concurrent.futures as futures
import hashlib
import itertools
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
DECK = "decks/AccomplishedAlchemist/AccomplishedAlchemist.cod"
OUTDIR = os.environ.get("AA_SWEEP_OUT", os.path.join(ROOT, "logs/aa_combo_off_sweep"))
SCRATCH = os.environ.get("AA_SWEEP_SCRATCH", OUTDIR)

_CARDS = None


def cards():
    global _CARDS
    if _CARDS is None:
        raw = json.load(open(os.path.join(ROOT, "src/cards/data/cards.json")))
        lst = raw["cards"] if isinstance(raw, dict) and "cards" in raw else raw
        _CARDS = {c["name"]: c for c in lst} if isinstance(lst, list) else lst
    return _CARDS


def deck_main_and_side():
    t = ET.parse(os.path.join(ROOT, DECK))
    main, side = {}, {}
    for z in t.getroot().iter("zone"):
        tgt = main if z.get("name") == "main" else side
        for c in z.iter("card"):
            tgt[c.get("name")] = tgt.get(c.get("name"), 0) + int(c.get("number"))
    return main, side


# ---------------------------------------------------------------------------------------------
# Fixtures and probes.
# ---------------------------------------------------------------------------------------------
def make_fixture(bf, hand, turn, *, opp_lib=40, opp_life=20, my_life=20, life_gained=0, library=None,
                 lib_size=30):
    """`bf` = [(name, tapped, sick, [aura names])]."""
    perms, aur = [], []
    for name, tapped, sick, auras in bf:
        e = {"name": name, "controller": 0, "tapped": bool(tapped)}
        if sick:
            e["sick"] = True
        perms.append(e)
        for a in auras:
            aur.append({"name": a, "controller": 0, "equips": name})
    fix = {"deck": DECK, "turn": turn, "on_the_play": True, "active_life": my_life,
           "opponent_life": opp_life, "opponent_library_size": opp_lib, "library_filler": "Forest",
           "library_size": lib_size, "depth": 3, "budget_ms": 20, "battlefield": perms + aur,
           "hand": list(hand)}
    if life_gained:
        fix["life_gained_this_turn"] = life_gained
        fix["resume_at"] = "main1"
    if library:
        fix["library_top"] = list(library)
    return fix


def state_id(family, fix):
    sig = json.dumps({
        "bf": sorted((p["name"], p.get("tapped", False), p.get("sick", False), p.get("equips", ""))
                     for p in fix["battlefield"]),
        "hand": sorted(fix["hand"]), "lib": fix.get("library_top", []), "turn": fix["turn"],
        "opp_lib": fix["opponent_library_size"], "opp_life": fix["opponent_life"],
        "lg": fix.get("life_gained_this_turn", 0)}, sort_keys=True)
    return "%s-%s" % (family, hashlib.sha1(sig.encode()).hexdigest()[:10])


OFFER_RE = re.compile(r"^scenario: combo_off plans=(\d+) offered=(\d) verified=(\d)"
                      r"(?: rule=(\S+))?(?: summary=\"(.*)\")?\s*$", re.M)
PROBE_RE = re.compile(r"^scenario: combo_off_rule_probe says=(\d) rule=(\S+)", re.M)
NOWIN_RE = re.compile(r"^scenario: FAIL combo_off plan applied but did NOT win"
                      r" \(opponent life (-?\d+), library (\d+)\)", re.M)
WIN_RE = re.compile(r"^scenario: win_turn=(\S+) opponent_life=(-?\d+) active_life=(-?\d+)", re.M)


def run_scenario(fix, env_extra=None):
    fd, path = tempfile.mkstemp(suffix=".json", dir=SCRATCH, text=True)
    with os.fdopen(fd, "w") as f:
        json.dump(fix, f)
    try:
        env = dict(os.environ)
        env.update(env_extra or {})
        p = subprocess.run([MTG, "--scenario", path], capture_output=True, text=True, cwd=ROOT, env=env)
        return p.returncode, p.stdout + p.stderr
    finally:
        try:
            os.unlink(path)
        except OSError:
            pass


def probe_offer(fix):
    f = dict(fix)
    f["env"] = {"MTG_HUMAN_PLAY": "1"}
    f["expect_combo_off"] = True
    f["combo_off_verify"] = True
    f["combo_off_rule_probe"] = True
    f["expect_history_no_macro"] = True
    _rc, out = run_scenario(f)
    res = {"offered": False, "verified": False, "rule": None, "rule_says": None, "apply_win": None,
           "opp_life_after": None, "opp_lib_after": None, "error": None, "macro": False}
    m = OFFER_RE.search(out)
    if not m:
        res["error"] = (out.strip().splitlines() or ["(no output)"])[-1][:200]
        return res
    res["offered"] = m.group(2) == "1"
    res["verified"] = m.group(3) == "1"
    res["rule"] = m.group(4)
    pr = PROBE_RE.search(out)
    if pr:
        res["rule_says"] = pr.group(1) == "1"
        res["probe_rule"] = pr.group(2)
    if res["offered"]:
        nw = NOWIN_RE.search(out)
        if nw:
            res["apply_win"] = False
            res["opp_life_after"] = int(nw.group(1))
            res["opp_lib_after"] = int(nw.group(2))
        elif "combo_off verified finish (opponent lost)" in out:
            res["apply_win"] = True
        elif "contains a MACRO entry" in out:
            res["macro"] = True
            res["apply_win"] = None
            res["error"] = "history macro"
        else:
            res["error"] = "offered but neither win nor no-win line"
    return res


def probe_win(fix, budget_ms=100):
    f = dict(fix)
    f.pop("env", None)
    # THE BOARD AS AUTHORED: resume at main 1 of this turn -- no untap, no draw, no land from the draw step.
    # Stepping in through an untap made every "sick Alchemist" and every tapped-out board a false class c
    # (the first sweep: 26 of its 52 c's were the untap step's doing).
    f["resume_at"] = "main1"
    f["max_turns"] = f["turn"]
    f["depth"] = 5
    f["budget_ms"] = budget_ms
    fd, logp = tempfile.mkstemp(suffix=".log.json", dir=SCRATCH)
    os.close(fd)
    f["log_out"] = logp
    _rc, out = run_scenario(f)
    # HOW the engine won: a Stroke of Genius at the opponent or a drain activation in the winning turn is a
    # COMBO kill (what the button is for); anything else is combat.
    kind = None
    try:
        g = json.load(open(logp))
        kind = "combat"
        for t in g.get("turns") or []:
            if t.get("turn") != fix["turn"]:
                continue
            for act in t.get("actions") or []:
                nm = act.get("cardName") or ""
                if act.get("type") == "CAST_SPELL" and nm == "Stroke of Genius" and any(
                        (x or {}).get("who") == "opponent" for x in act.get("targets") or []):
                    kind = "combo"
                if act.get("type") == "ABILITY" and "Scholar" in nm:
                    kind = "combo"
    except Exception:
        kind = None
    finally:
        try:
            os.unlink(logp)
        except OSError:
            pass
    m = WIN_RE.search(out)
    if not m:
        return {"auto_win_turn": None, "error": (out.strip().splitlines() or ["?"])[-1][:200]}
    wt = m.group(1)
    return {"auto_win_turn": (None if wt == "none" else int(wt)), "auto_opp_life": int(m.group(2)),
            "kind": kind, "error": None}


# ---------------------------------------------------------------------------------------------
# POPULATION 1 -- the synthetic matrix over the rule table's ingredients.
# ---------------------------------------------------------------------------------------------
def synthetic_states(quick=False):
    out = []
    LIFE = {"none": [], "nourish": ["Nourish"], "feed": ["Feed the Clan"], "weather": ["Weather the Storm"],
            "gained6": "G6"}
    LOOP = {
        "umbra_bf": ("bf_aura", "Crab Umbra"), "umbra_hand": ("hand", "Crab Umbra"),
        "staff_bf": ("bf", "Staff of Domination"), "staff_hand": ("hand", "Staff of Domination"),
        "witch_bf": ("bf", "Seedcradle Witch"), "witch_hand": ("hand", "Seedcradle Witch"),
        "galv_hand": ("hand", "Galvanic Alchemist"), "none": (None, None)}
    FIN = {"stroke": ["Stroke of Genius"], "wish": ["Living Wish"], "scholar_hand": ["Scholar of Athreos"],
           "none": []}
    LANDS = [2, 4, 6] if not quick else [3, 5]
    ISLAND = [True, False] if not quick else [True]
    SICK = [False, True] if not quick else [False]
    lifes = list(LIFE) if not quick else ["none", "nourish", "gained6"]
    loops = list(LOOP) if not quick else ["umbra_bf", "umbra_hand", "staff_bf", "none"]
    fins = list(FIN) if not quick else ["stroke", "wish", "none"]
    for life, loop, fin, nl, isl, sick in itertools.product(lifes, loops, fins, LANDS, ISLAND, SICK):
        if sick and (life != "nourish" or loop not in ("umbra_bf", "staff_bf")):
            continue                               # one sick family is enough to pin the refusal
        bf = [("Forest", False, False, []) for _ in range(nl - (1 if isl else 0))]
        if isl:
            bf.append(("Island", False, False, []))
        alc_auras = []
        hand = []
        where, card = LOOP[loop]
        if where == "bf_aura":
            alc_auras.append(card)
        elif where == "bf":
            bf.append((card, False, False, []))
        elif where == "hand":
            hand.append(card)
        bf.append(("Accomplished Alchemist", False, sick, alc_auras))
        lg = 0
        if LIFE[life] == "G6":
            lg = 6
        else:
            hand += LIFE[life]
        hand += FIN[fin]
        fix = make_fixture(bf, hand, 5, life_gained=lg)
        out.append(("S", fix, "%s/%s/%s lands=%d island=%d sick=%d" % (life, loop, fin, nl, isl, sick)))
    return out


# ---------------------------------------------------------------------------------------------
# POPULATION 2 -- autonomous engine play.
# ---------------------------------------------------------------------------------------------
def run_engine_games(games, seed, threads, gamedir, reuse=False, extra=None):
    if reuse and os.path.isdir(gamedir) and os.listdir(gamedir):
        return 0, "(reused %d existing game logs)" % len(os.listdir(gamedir))
    if os.path.isdir(gamedir):
        shutil.rmtree(gamedir)
    os.makedirs(gamedir, exist_ok=True)
    args = [MTG, DECK, "--games", str(games), "--seed", str(seed), "--max-turns", "8", "--depth", "3",
            "--budget-ms", "10", "--log-dir", gamedir, "--threads", str(threads)] + (extra or [])
    p = subprocess.run(args, capture_output=True, text=True, cwd=ROOT)
    return p.returncode, (p.stdout + p.stderr)[-2000:]


def engine_states(gamedir):
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
        cast_gone = set()
        for pi, pe in enumerate(phases):
            for a in pe.get("actions") or []:
                if a.get("type") == "CAST_SPELL":
                    cast_gone.add(a.get("card"))
            nxt = phases[pi + 1] if pi + 1 < len(phases) else None
            if nxt is None or not (pe.get("phase") == "DRAW" and nxt.get("phase") == "MAIN_1"
                                   and nxt.get("turn") == pe.get("turn")):
                continue
            board = pe.get("boardAfter") or {}
            bfl = board.get("battlefield") or []
            hosts, perms, on_bf = {}, [], set()
            for c in bfl:
                if c.get("attachedTo"):
                    hosts.setdefault(c["attachedTo"], []).append(c["cardName"])
            turn = pe.get("turn")
            for c in bfl:
                on_bf.add(c.get("card"))
                if c.get("attachedTo"):
                    continue
                # At the start of MAIN_1 every permanent arrived on an EARLIER turn: nothing is sick.
                perms.append((c["cardName"], bool(c.get("tapped")), False, hosts.get(c.get("card"), [])))
            hand_nums = board.get("hand") or []
            hand = [num2name.get(n, "") for n in hand_nums]
            hand = [h for h in hand if h]
            gy = set(board.get("graveyard") or [])
            seen = set(hand_nums) | on_bf | gy | (cast_gone - on_bf)
            lib = [num2name[n] for n in sorted(num2name) if n not in seen and n in num2name
                   and num2name[n] in main]
            fix = make_fixture(perms, hand, turn, opp_lib=max(1, 53 - (turn - 1)),
                               opp_life=board.get("opponentLife", 20), my_life=board.get("playerLife", 20),
                               library=lib, lib_size=1)
            out.append(("X", fix, json.dumps({"game": fn, "turn": turn, "win_turn": win_turn})))
    return out


# ---------------------------------------------------------------------------------------------
def classify(rec):
    o = rec["offer"]
    if o.get("error"):
        return "x_error"
    if o["offered"]:
        if o["apply_win"]:
            return "a_offered_wins"
        w = rec["win"].get("auto_win_turn") is not None or rec["win10"].get("auto_win_turn") is not None
        if w and rec["win_kind"] == "combo":
            return "b_offered_no_win"
        # The autonomous engine wins this turn only in COMBAT (or not at all): the rule promised a main-phase
        # kill no executor reached -- the rule is too loose for THIS board, whatever combat can do.
        return "d_rule_too_loose"
    w = rec["win"].get("auto_win_turn")
    if w is not None:
        return "c_missed_offer" if rec["win_kind"] == "combo" else "cprime_combat"
    return "e_absent_unwinnable"


def analyse_one(item):
    family, fix, note = item
    rec = {"id": state_id(family, fix), "family": family, "note": note, "turn": fix["turn"],
           "untap_confound": any(p.get("tapped") for p in fix["battlefield"]), "win": {}, "win10": {}}
    rec["offer"] = probe_offer(fix)
    o = rec["offer"]
    if (not o["offered"]) or o["apply_win"] is False:
        rec["win"] = probe_win(fix)
        if o["offered"] and rec["win"].get("auto_win_turn") is None:
            rec["win10"] = probe_win(fix, budget_ms=1000)
    # combo vs combat: did the opponent die with life still > 0 (decked) or with no creature able to
    # attack? Approximate; see the module docstring.
    power = 0
    for p in fix["battlefield"]:
        c = cards().get(p["name"]) or {}
        if p.get("equips") or p.get("sick") or p.get("tapped"):
            continue
        try:
            power += int(c.get("power") or 0)
        except (TypeError, ValueError):
            pass
    rec["board_power"] = power
    rec["win_kind"] = rec["win"].get("kind") or rec["win10"].get("kind") or "combat"
    rec["fixture"] = fix
    rec["class"] = classify(rec)
    return rec


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--quick", action="store_true")
    ap.add_argument("--games", type=int, default=40)
    ap.add_argument("--seed", type=int, default=91001)
    ap.add_argument("--threads", type=int, default=max(1, (os.cpu_count() or 4) - 2))
    ap.add_argument("--reuse-games", action="store_true")
    ap.add_argument("--no-engine", action="store_true")
    ap.add_argument("--no-synthetic", action="store_true")
    a = ap.parse_args()
    os.makedirs(OUTDIR, exist_ok=True)
    os.makedirs(SCRATCH, exist_ok=True)
    items = []
    if not a.no_synthetic:
        items += synthetic_states(a.quick)
    if not a.no_engine:
        gamedir = os.path.join(SCRATCH, "games")
        games = 10 if a.quick else a.games
        rc, tail = run_engine_games(games, a.seed, a.threads, gamedir, a.reuse_games)
        if rc != 0:
            print("engine games failed:\n" + tail, file=sys.stderr)
            return 2
        items += engine_states(gamedir)
    seen, uniq = set(), []
    for it in items:
        sid = state_id(it[0], it[1])
        if sid in seen:
            continue
        seen.add(sid)
        uniq.append(it)
    print("states: %d (%d synthetic, %d engine)" % (len(uniq), sum(1 for u in uniq if u[0] == "S"),
                                                   sum(1 for u in uniq if u[0] == "X")), flush=True)
    recs = []
    with futures.ThreadPoolExecutor(max_workers=a.threads) as ex:
        for r in ex.map(analyse_one, uniq):
            recs.append(r)
    by = {}
    for r in recs:
        by.setdefault((r["family"], r["class"]), []).append(r)
    classes = ["a_offered_wins", "b_offered_no_win", "c_missed_offer", "cprime_combat", "d_rule_too_loose",
               "e_absent_unwinnable", "x_error"]
    print("%-10s %s" % ("pop", " ".join("%8s" % c.split("_")[0] for c in classes)))
    for fam, label in (("S", "synthetic"), ("X", "engine")):
        print("%-10s %s" % (label, " ".join("%8d" % len(by.get((fam, c), [])) for c in classes)))
    rules = {}
    for r in recs:
        if r["offer"]["offered"]:
            k = r["offer"]["rule"] or "?"
            w = rules.setdefault(k, [0, 0])
            w[0] += 1
            w[1] += 1 if r["offer"]["apply_win"] else 0
    print("\nper rule (offered / wins):")
    for k in sorted(rules):
        print("  %-48s %4d / %4d" % (k, rules[k][0], rules[k][1]))
    for c in ("b_offered_no_win", "c_missed_offer", "d_rule_too_loose", "x_error"):
        rs = [r for r in recs if r["class"] == c]
        if rs:
            print("\n%s (%d):" % (c, len(rs)))
            for r in rs[:25]:
                print("  %s %s rule=%s says=%s %s" % (r["id"], r["note"][:90], r["offer"].get("rule"),
                                                      r["offer"].get("rule_says"), r["offer"].get("error") or ""))
    json.dump(recs, open(os.path.join(OUTDIR, "results.json"), "w"), indent=1)
    print("\nresults: %s" % os.path.join(OUTDIR, "results.json"))
    return 0


if __name__ == "__main__":
    sys.exit(main())

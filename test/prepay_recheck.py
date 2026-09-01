#!/usr/bin/env python3
"""Re-evaluation set for the 2026-08-25 colour-honesty batch (90a537bd..c6743509).

WHY THIS EXISTS
    That batch made three mana fixes. Two are unambiguous rules fixes. The third
    (MTG_PREPAY_TRUE_COLOURS, "the prepaid pool keeps its true colours") is also a real
    soundness fix, but it changed the PAYMENT path, and on Mirrorwing it was caught paying
    for an identical line by tapping a mana dork the pre-fix path spared -- which removed an
    attacker and turned a T4 kill into a T6 win. See docs/design/prepay-payment-path-recheck.md.

    Ground truth was rebaselined WITH that defect in it. This file is the list of every game
    whose outcome that batch changed for the worse, so it can be re-run once the payment path
    is repaired and the ones that recover can be rebaselined back.

    The case list is SELF-CONTAINED: deck, profile, seed, game index, depth and budget are
    baked in, so a checkout with unpushed work can verify without this repo's gt_logs or
    explain_game.py.

USAGE
    build     python3 test/prepay_recheck.py build [--baseline 90a537bd]
              Regenerate cases.tsv from `git show <baseline>:test/gt_logs/*` vs the working
              tree's gt_logs. Only needed if the set itself must be recomputed.

    verify    python3 test/prepay_recheck.py verify [--jobs N] [--bin PATH] [--filter SUBSTR]
              Run every case on the CURRENT binary and report, per game, whether it now
              matches the PRE-fix outcome (RECOVERED), still matches the post-fix one
              (STILL-WORSE), or landed somewhere else (MOVED).
              This is the one an agent working on the payment path wants.

    classify  python3 test/prepay_recheck.py classify [--jobs N] [--filter SUBSTR]
              Expensive. For each case, attribute it to a switch and force BOTH arms down the
              recorded line. The interesting class is EXECUTION-DIFFERS: both arms play the
              identical line and still disagree on the result, which cannot be a search or
              ranking effect -- it is the payment path.

    tapdiff   python3 test/prepay_recheck.py tapdiff [--jobs N]
              Force both arms down the recorded line and diff the NON-LAND permanents tapped
              before combat. A tapped creature cannot attack -- this is the dork-tap signature.

    adjudicate python3 test/prepay_recheck.py adjudicate [--jobs N]
              Reads logs/prepay_tapdiff.json (run tapdiff first) and SETTLES each TAP-ORDER case
              from the control's own game log: reconstruct the pre-combat bill and every mana
              source that was available, and ask whether the bill was payable WITHOUT the disputed
              creature. DEFECT = it was, so the tap was a choice. WARRANTED = it was not.
              No engine change: board state per phase plus each cast's `manaPaid` is enough.

    legality  python3 test/prepay_recheck.py legality [--jobs N]
              The one question the same data answers for ALL 418, tap-order or not: was the
              PRE-FIX line's own payment colour-legal on every turn? LAUNDERED means `pre_fix`
              came off a payment the rules forbid, so the game getting worse is the fix working.

EXIT CODE
    verify exits 0 always; read the summary. It is a report, not a gate.
"""
import argparse, concurrent.futures as futures, json, os, re, subprocess, sys, tempfile, shutil, glob

ROOT  = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CASES = os.path.join(ROOT, "test", "prepay_recheck_cases.tsv")
COLS  = ["key", "gi", "deck", "profile", "seed", "game_index", "depth", "budget",
         "pre_fix", "post_fix", "attribution", "walk_class", "verdict", "spend"]
# `verdict` is filled by `adjudicate` and is blank for the 393 cases that never carried the
# tap-order signature. DEFECT there means: that game has a turn PROVEN payable without the mana
# creature the current engine taps, so a repaired payment path should move it.
# `spend` is filled by `legality` for ALL 418: LAUNDERED = the PRE-FIX line paid a coloured pip its
# own sources could not make, so `pre_fix` came off a payment the rules forbid.

# The batch's off-switches. Setting all of them reproduces the PRE-batch engine in the SAME
# executable, which is the only valid control: src/cards/data/cards.json is read at RUNTIME and
# one of the fixes IS card data, so an old BUILD run from this tree loads the new card file.
LEGACY_ENV = {"MTG_PREPAY_TRUE_COLOURS": "0", "MTG_LEGACY_RITUAL_WILD": "1",
              "MTG_LEGACY_STAGED_SUSPEND": "1", "MTG_AURA_RANK_MODE": "1",
              "MTG_NO_LINE_HOLD": "1", "MTG_LEGACY_BESTOW_SIG": "1"}
SWITCHES = [("prepay-colours",   {"MTG_PREPAY_TRUE_COLOURS": "0"}),
            ("ritual-colours",   {"MTG_LEGACY_RITUAL_WILD": "1"}),
            ("staged-suspend",   {"MTG_LEGACY_STAGED_SUSPEND": "1"}),
            ("aura-fetch-order", {"MTG_AURA_RANK_MODE": "1"}),
            ("line-hold",        {"MTG_NO_LINE_HOLD": "1"}),
            ("bestow-signature", {"MTG_LEGACY_BESTOW_SIG": "1"})]

AVG_RE = re.compile(r"avg \(turns\)\s*:\s*([0-9.]+)")


def mode_of(key):
    """Tier name out of a case key. NOT key.split('_')[1] -- `creature_giving_overnight_d0_s4004`
    has an underscore in the DECK name, and that slip silently dropped all 19 creature_giving
    cases from the first build of this list."""
    for m in ("smoke", "regression", "overnight"):
        if f"_{m}_" in key:
            return m
    return "regression"


def read_cases(filt=None):
    rows = []
    with open(CASES) as fh:
        for ln in fh:
            ln = ln.rstrip("\n")
            if not ln or ln.startswith("#"):
                continue
            parts = ln.split("\t")
            r = dict(zip(COLS, parts + [""] * (len(COLS) - len(parts))))
            if filt and filt not in r["key"]:
                continue
            rows.append(r)
    return rows


def score_once(binary, r, env_extra=None):
    """Loss-penalized score of ONE autonomous game -- the same number ground truth records."""
    tmp = tempfile.mkdtemp(prefix="recheck_")
    try:
        cmd = [binary, os.path.join(ROOT, r["deck"]), "--profile", os.path.join(ROOT, r["profile"]),
               "--games", "1", "--seed", r["seed"], "--game-index", r["game_index"],
               "--depth", r["depth"], "--budget-ms", r["budget"],
               "--ignore-play-profile", "--log-dir", tmp]
        out = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True,
                             env=dict(os.environ, **(env_extra or {}))).stdout
        m = AVG_RE.search(out)
        return float(m.group(1)) if m else None
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


# ---------------------------------------------------------------- build
def cmd_build(args):
    keys = sorted(f[:-5] for f in os.listdir(os.path.join(ROOT, "test/gt_logs")) if f.endswith(".wins"))
    sys.path.insert(0, os.path.join(ROOT, "test"))
    sys.path.insert(0, ROOT)
    import explain_game as explain                      # build-time only; verify does not need it
    cfg = explain.load_cases()
    rows = []
    for k in keys:
        old = subprocess.run(["git", "show", f"{args.baseline}:test/gt_logs/{k}.wins"],
                             cwd=ROOT, capture_output=True, text=True).stdout
        if not old.strip():
            continue
        new = open(os.path.join(ROOT, "test/gt_logs", f"{k}.wins")).read()
        mode = mode_of(k)
        try:
            r = explain.resolve(mode, k, cfg)
        except Exception:
            continue
        for lo, ln in zip(old.splitlines(), new.splitlines()):
            a, b = lo.split(), ln.split()
            if a[0] != b[0]:
                continue
            ow, nw = int(a[1]), int(b[1])
            if ow == nw:
                continue
            worse = (nw > ow or nw == -1) and ow != -1     # -1 == no win inside max_turns
            if not worse:
                continue
            gi = int(a[0])
            rows.append([k, gi, r["deckfile"], r["profile"], r["seed"] + gi, gi,
                         r["depth"], r["budget"], ow, nw, "", "", "", ""])
    rows.sort(key=lambda x: (x[0], x[1]))
    with open(CASES, "w") as fh:
        fh.write("# Games the 2026-08-25 colour-honesty batch made WORSE (baseline %s).\n" % args.baseline)
        fh.write("# pre_fix = win turn before the batch, post_fix = win turn now (-1 = no win).\n")
        fh.write("# " + "\t".join(COLS) + "\n")
        for r in rows:
            fh.write("\t".join(str(x) for x in r) + "\n")
    print(f"wrote {len(rows)} cases -> {os.path.relpath(CASES, ROOT)}")
    return 0


# ---------------------------------------------------------------- verify
MAX_TURNS = 8          # mtg's default; the harness does not override it


def _score(v):
    """gt_logs writes an unwon game as -1; a run reports it as the loss-penalized max_turns+1. Compare
    them raw and every lost game reads MOVED -- which is how the one `post_fix=-1` case in the DEFECT
    set showed up as "now=9.0, MOVED" against a binary that had not changed at all."""
    v = float(v)
    return float(MAX_TURNS + 1) if v < 0 else v


def verify_one(binary, r):
    now = score_once(binary, r)
    pre, post = _score(r["pre_fix"]), _score(r["post_fix"])
    if now is None:
        cls = "ERROR"
    elif now <= pre:
        cls = "RECOVERED"
    elif abs(now - post) < 1e-9:
        cls = "STILL-WORSE"
    else:
        cls = "MOVED"
    return dict(r, now=now, cls=cls)


def cmd_verify(args):
    binary = args.bin or os.path.join(ROOT, "build/Release/mtg")
    rows = read_cases(args.filter)
    if getattr(args, "defects", False):
        rows = [r for r in rows if r.get("verdict") == "DEFECT"]
    print(f"VERIFY  {len(rows)} cases  bin={binary}")
    print("  RECOVERED  = now at or better than the PRE-fix turn (the payment-path defect is gone here)")
    print("  STILL-WORSE= unchanged from the rebaselined value")
    print("  MOVED      = a third value; inspect before drawing a conclusion\n")
    out, agg = [], {}
    with futures.ThreadPoolExecutor(max_workers=args.jobs) as ex:
        for a in ex.map(lambda r: verify_one(binary, r), rows):
            out.append(a)
            agg[a["cls"]] = agg.get(a["cls"], 0) + 1
            if a["cls"] != "STILL-WORSE":
                print(f"  {a['key']} gi{a['gi']}: pre={a['pre_fix']} post={a['post_fix']} "
                      f"now={a['now']}  <- {a['cls']}", flush=True)
    print("\nsummary: " + ", ".join(f"{k}={v}" for k, v in sorted(agg.items())))
    by_deck = {}
    for a in out:
        d = a["key"].split("_")[0]
        by_deck.setdefault(d, {}).setdefault(a["cls"], 0)
        by_deck[d][a["cls"]] += 1
    print("\nby deck:")
    for d in sorted(by_deck):
        print(f"  {d:18s} " + ", ".join(f"{k}={v}" for k, v in sorted(by_deck[d].items())))
    with open(os.path.join(ROOT, "logs", "prepay_recheck_verify.json"), "w") as fh:
        json.dump(out, fh, indent=1)
    print("\nper-case detail -> logs/prepay_recheck_verify.json")
    return 0


# ---------------------------------------------------------------- classify
def classify_one(binary, r):
    """Attribute the game to a switch, then force BOTH arms down the recorded line."""
    now  = score_once(binary, r)
    allo = score_once(binary, r, LEGACY_ENV)
    hits = []
    if allo is not None and now is not None and allo != now:
        for name, env in SWITCHES:
            if score_once(binary, r, env) == allo:
                hits.append(name)
    attribution = ",".join(hits) or ("(none)" if allo == now else "(combination)")

    walk = ""
    if "prepay-colours" in hits or attribution == "(combination)":
        try:
            sys.path.insert(0, os.path.join(ROOT, "test"))
            import gt_line_playable as G
            mode = mode_of(r["key"])
            res = G.check_game(mode, r["key"], int(r["gi"]), binary, binary)
            if res.get("status") == "WALKED":
                ow, ctl, rep = res.get("old_win"), res.get("old_self"), res.get("replay_win")
                b, cb = res.get("blocked"), res.get("ctrl_blocked")
                # A refusal the CONTROL reproduces is the forced walk drifting off the recorded
                # line (a defaulted scry reorders the library), NOT the engine refusing it. Only a
                # refusal the control does not share is evidence -- same bar gt_line_playable uses.
                if b and cb and cb.get("turn") == b.get("turn"):
                    walk = "INCONCLUSIVE"
                elif b:
                    walk = "REFUSED"
                elif ctl == ow and rep != ow:
                    walk = "EXECUTION-DIFFERS"      # identical forced line, different result
                elif ctl == ow and rep == ow:
                    walk = "CHOICE-ONLY"            # the line still executes; only the pick moved
                else:
                    walk = "INCONCLUSIVE"
            else:
                walk = res.get("status", "?")
        except Exception as e:
            walk = f"ERR:{type(e).__name__}"
    return dict(r, attribution=attribution, walk_class=walk)


def cmd_classify(args):
    binary = args.bin or os.path.join(ROOT, "build/Release/mtg")
    rows = read_cases(args.filter)
    print(f"CLASSIFY  {len(rows)} cases  (attribute; then force both arms for prepay-attributed ones)")
    done, agg, wagg = [], {}, {}
    with futures.ThreadPoolExecutor(max_workers=args.jobs) as ex:
        for a in ex.map(lambda r: classify_one(binary, r), rows):
            done.append(a)
            agg[a["attribution"]] = agg.get(a["attribution"], 0) + 1
            if a["walk_class"]:
                wagg[a["walk_class"]] = wagg.get(a["walk_class"], 0) + 1
            print(f"  {a['key']} gi{a['gi']}: {a['pre_fix']}->{a['post_fix']}  "
                  f"{a['attribution']}  {a['walk_class']}", flush=True)
    print("\nattribution: " + ", ".join(f"{k}={v}" for k, v in sorted(agg.items())))
    print("walk class : " + ", ".join(f"{k}={v}" for k, v in sorted(wagg.items())))
    by = {r["key"] + "|" + str(r["gi"]): r for r in done}
    rows_all = read_cases()
    with open(CASES, "w") as fh:
        fh.write("# Games the 2026-08-25 colour-honesty batch made WORSE.\n")
        fh.write("# pre_fix = win turn before the batch, post_fix = win turn now (-1 = no win).\n")
        fh.write("# " + "\t".join(COLS) + "\n")
        for r in rows_all:
            k = r["key"] + "|" + str(r["gi"])
            if k in by:
                r = by[k]
            fh.write("\t".join(str(r[c]) for c in COLS) + "\n")
    print(f"\nannotated -> {os.path.relpath(CASES, ROOT)}")
    return 0


# ---------------------------------------------------------------- tapdiff
# The dork-tap signature, mechanised. Adjudicating mirrorwing gi309 by hand came down to one
# comparison: at the pre-combat decision, is a NON-LAND permanent tapped in the fixed arm that the
# control left untapped? A tapped creature cannot attack, which is what actually costs the game.
# Combat itself taps attackers, so the comparison must be made BEFORE combat -- the last decision in
# the turn at which the opponent's life is still the turn's opening value.
_tls = __import__("threading").local()


def _logging_play_step(bin_, r, gi, force, choices, validate=None, max_turns=None, env_extra=None):
    args = [bin_, os.path.join(ROOT, r["deckfile"]), "--claude-play",
            "--profile", os.path.join(ROOT, r["profile"]), "--seed", str(r["seed"] + gi),
            "--game-index", str(gi), "--depth", str(r["depth"]), "--budget-ms", str(r["budget"]),
            "--ignore-play-profile", "--force-mulligan", force]
    if max_turns is not None:
        args += ["--max-turns", str(max_turns)]
    if choices:
        args += ["--choices", ",".join(str(c) for c in choices)]
    if validate is not None:
        args += ["--validate-line", validate]
    d = getattr(_tls, "logdir", None)
    if d:
        args += ["--log-dir", d]
    p = subprocess.run(args, cwd=ROOT, capture_output=True, text=True,
                       env=dict(os.environ, MTG_PLAY_PLANS_CAP="0", **(env_extra or {})))
    return p.returncode, p.stdout + p.stderr


def _precombat_tapped(logdir):
    """{turn: set(non-land permanents tapped before combat)} from a --claude-play decision log."""
    files = glob.glob(os.path.join(logdir, "*.json"))
    if not files:
        return {}, None
    g = json.load(open(files[0]))
    by_turn = {}
    for rec in g.get("decisions", []):
        d = rec["decision"]
        t = d.get("turn")
        if t is None:
            continue
        by_turn.setdefault(t, []).append(d)
    out = {}
    for t, decs in by_turn.items():
        lives = [d.get("opponent", {}).get("life") for d in decs if d.get("opponent")]
        if not lives:
            continue
        opening = max(x for x in lives if x is not None)
        pre = [d for d in decs if d.get("opponent", {}).get("life") == opening]
        if not pre:
            continue
        d = pre[-1]
        out[t] = {c["name"] for c in d.get("me", {}).get("battlefield", [])
                  if not c.get("is_land") and c.get("tapped")}
    return out, g.get("win_turn")


def tapdiff_one(binary, r):
    import gt_line_playable as G
    G.play_step = _logging_play_step                      # route every walk through --log-dir
    gi, key = int(r["gi"]), r["key"]
    base = tempfile.mkdtemp(prefix="tapdiff_")
    try:
        cfg = G.explain.load_cases()
        rr = G.explain.resolve(mode_of(key), key, cfg)
        og = G.old_game(binary, rr, gi, G.LEGACY_ENV)
        if og is None:
            return dict(r, tap_class="NO-OLD-LOG", tap_detail="")
        turns, force = og[0], og[1]
        caps, res = [], {}
        for tag, env, use_hints in (("off", G.LEGACY_ENV, False), ("on", None, True)):
            d = os.path.join(base, tag)
            os.makedirs(d, exist_ok=True)
            _tls.logdir = d
            G.walk_line(binary, rr, gi, force, turns,
                        hints=(caps if use_hints else None),
                        capture=(None if use_hints else caps), tag=tag, env_extra=env)
            res[tag] = _precombat_tapped(d)
        _tls.logdir = None
        off, on = res["off"][0], res["on"][0]
        hits = []
        for t in sorted(set(off) & set(on)):
            extra = on[t] - off[t]
            if extra:
                hits.append(f"T{t}:" + "+".join(sorted(extra)))
        if hits:
            return dict(r, tap_class="TAP-ORDER", tap_detail=";".join(hits))
        return dict(r, tap_class="no-tap-diff", tap_detail="")
    except Exception as e:
        return dict(r, tap_class=f"ERR:{type(e).__name__}", tap_detail="")
    finally:
        shutil.rmtree(base, ignore_errors=True)


def cmd_tapdiff(args):
    binary = args.bin or os.path.join(ROOT, "build/Release/mtg")
    rows = read_cases(args.filter)
    print(f"TAPDIFF  {len(rows)} cases -- non-land permanents tapped pre-combat in the FIXED arm")
    print("         that the control left untapped. A tapped creature cannot attack.\n")
    done, agg = [], {}
    with futures.ThreadPoolExecutor(max_workers=args.jobs) as ex:
        for a in ex.map(lambda r: tapdiff_one(binary, r), rows):
            done.append(a)
            agg[a["tap_class"]] = agg.get(a["tap_class"], 0) + 1
            if a["tap_class"] == "TAP-ORDER":
                print(f"  {a['key']} gi{a['gi']}: {a['pre_fix']}->{a['post_fix']}  "
                      f"[{a['attribution']}]  {a['tap_detail']}", flush=True)
    print("\nsummary: " + ", ".join(f"{k}={v}" for k, v in sorted(agg.items())))
    with open(os.path.join(ROOT, "logs", "prepay_tapdiff.json"), "w") as fh:
        json.dump(done, fh, indent=1)
    print("detail -> logs/prepay_tapdiff.json")
    return 0


# ---------------------------------------------------------------- adjudicate
# `tapdiff` says the fixed arm spends a mana creature the control did not. That is a SIGNATURE, not a
# verdict: if the control was paying with laundered mana it never legally had, the fixed arm is
# *right* to go looking for a real source, and a mana dork is a real source. The question that
# actually settles a case is the one gi309 was settled by hand on:
#
#     was the turn's cost payable, under TRUE colours, from the sources the control actually spent?
#
# The control never taps the disputed creature (that is what tapdiff proved), so its own spend set is
# a witness that excludes it. If that witness set pays the bill under true colours, the pre-fix line
# needed no laundering and the extra tap is avoidable -> DEFECT. If it does not, the control was
# laundering and the regression is correct behaviour -> WARRANTED.
#
# Everything here reads the CONTROL's own autonomous game log -- board state per phase plus each
# cast's `manaPaid` -- so no engine change and no re-derivation is involved.
CARDS_JSON = os.path.join(ROOT, "src", "cards", "data", "cards.json")
ALL_COLORS = frozenset("WUBRG")
ANY_PIP    = frozenset("WUBRGC")          # a generic pip: colourless mana pays it too
_PIP_RE    = re.compile(r"\{([^}]*)\}")


def _units_for(card):
    """Mana UNITS a source yields when tapped, as a list of producible-colour sets, plus the mana it
    COSTS to activate (filter/signet lands are net +1, not free). None = not a mana source.

    `produces_amount == len(produces) > 1` is "add one of each" (Gruul Turf's {R}{G}, Izzet
    Boilerworks' {U}{R}) -- the engine audits exactly that distinction as an illegal bundle tap
    (GameLogger.cpp NoteIllegalBundleTap), so it must not be read as "N of any listed colour"."""
    p = card.get("parameters", {})
    if p.get("sac_for_mana_amount"):                      # Treasure: {T}, sac: one mana of any colour
        return [ALL_COLORS] * int(p["sac_for_mana_amount"]), 0
    prod = p.get("produces")
    if not prod:
        return None
    n = int(p.get("produces_amount", 1))
    cols = [frozenset([c]) if c != "C" else frozenset("C") for c in prod]
    if n > 1 and len(prod) == n:   units = cols                     # one of each
    elif len(prod) == 1:           units = [cols[0]] * n
    elif n == 1:                   units = [frozenset(prod)]        # any ONE of the listed colours
    else:                          return None                      # shape we do not model
    # A filter/signet is not free: it CONSUMES mana to run. The input matters -- Cascade Bluffs costs
    # {U/R}, so a Sol Ring's colourless cannot feed it, while a Signet's {1} can be fed by anything.
    if p.get("is_filter"):         return units * 2, frozenset(prod)   # {U/R},{T}: add two of U/R
    # Signet ({1},{T}: add {U}{R}): ONE UNIT PER produces COLOUR, feeder any. The old `units`
    # return here was the single choice-unit [frozenset(prod)] -- gross 1 in exchange for the
    # {1} feeder = NET ZERO, while both the real card and the engine (ManaPayment's ramp-filter
    # branch floats EVERY produces colour) are net +1. Every turn a Signet tapped was therefore
    # under-credited by one unit -- a "short(n<m)" verdict away from spurious. Found 2026-09-01
    # during the Spasm literal-untap line-payability analysis.
    if p.get("ramp_filter"):       return cols, ANY_PIP
    return units, 0


def _card_db():
    if not hasattr(_card_db, "v"):
        db = {c["name"]: c for c in json.load(open(CARDS_JSON))["cards"]}
        _card_db.v = db
    return _card_db.v


def _enters_tapped_always(card):
    """Does this land ALWAYS enter tapped? Then a copy played this turn cannot have paid for
    anything, and must not be counted as a source.

    Three ways to enter untapped, and all three must be recognised or a real source is dropped:
    a reveal (Game Trail), 2 life (the shocklands), and a board condition -- "enters tapped UNLESS
    you control two or fewer other lands" (Razorverge Thicket, Rootbound Crag). Missing that last
    one made every Razorverge turn-1 read "0 sources for 1 pip"."""
    p = card.get("parameters", {})
    if p.get("etb_untap_reveal_subtypes") or p.get("etb_pay_life_to_untap"):
        return False
    o = (card.get("oracle_text") or "").lower()
    if "enters tapped" not in o and "enters the battlefield tapped" not in o:
        return False
    return "unless" not in o


def _parse_cost(s):
    """'{1}{G}' -> ([pip colour-sets], ok). Returns ok=False for anything we refuse to price."""
    pips, pos = [], 0
    for m in _PIP_RE.finditer(s or ""):
        if m.start() != pos:
            return [], False
        pos = m.end()
        t = m.group(1)
        if t.isdigit():                       pips += [ANY_PIP] * int(t)
        elif len(t) == 1 and t in "WUBRGC":    pips.append(frozenset("C") if t == "C" else frozenset(t))
        elif "/" in t and all(x in "WUBRG" for x in t.split("/")):
            pips.append(frozenset(t.split("/")))          # hybrid
        else:                                  return [], False   # {X}, phyrexian, snow, ...
    return pips, (pos == len(s or ""))


def _payable(pips, units):
    """Is every pip assignable to a DISTINCT unit it accepts? Kuhn's bipartite matching.
    Coloured pips are matched first (generic accepts anything, so it never blocks a colour)."""
    if len(pips) > len(units):
        return False
    order = sorted(range(len(pips)), key=lambda i: len(pips[i]))
    match = [-1] * len(units)

    def aug(pi, seen):
        for ui, u in enumerate(units):
            if ui in seen or not (pips[pi] & u):
                continue
            seen.add(ui)
            if match[ui] < 0 or aug(match[ui], seen):
                match[ui] = pi
                return True
        return False

    return all(aug(pi, set()) for pi in order)


def _turn_ledger(game, turn, db, precombat_only=True):
    """For one turn of a game log: the mana UNITS the player spent across its main phases, the PIPS
    those mains paid, and the notes that bound how far the ledger can be trusted.

    `precombat_only` counts only the mains BEFORE combat. That is the segment where a tapped creature costs an
    attack, and it is where `tapdiff` samples -- span the whole turn instead and a source the control
    tapped in main 2 is booked as though it had been available before combat, which is how gi107's
    Ornithopter (tapped post-combat by the control) inflated the pre-combat pool.

    Sources are read as a tapped-delta against the record immediately before each MAIN, plus
    permanents played-and-tapped inside the main itself (the turn's land, routinely tapped the moment
    it lands) and permanents that VANISHED mid-main.

    The vanished ones are the subtle part. A Treasure disappears BECAUSE it was sacrificed for mana;
    Sandstone Needle sacrifices itself as its last depletion counter comes off; a Karoo land
    (Gruul Turf) bounces a land that was, in the ordinary line, tapped for mana first. Dropping them
    is what made four cases read "4 pips vs 1 units". They are counted separately as `maybe` so a
    verdict can say whether it leaned on them.

    Notes are split by which side of the ledger they under-count, because that decides which verdicts
    stay sound:
      soft -> UNITS are under-counted (an untap-refloat taps a source twice; an uncoloured ritual
              adds mana this checker will not colour). Payable-anyway still proves DEFECT.
      hard -> PIPS are under-counted or unparseable ({X} costs). Nothing is provable either way."""
    recs = [t for t in game.get("turns", []) if t.get("turn") == turn]
    units, maybe, bill, fcost, soft, hard = [], [], [], [], [], []

    def board(rec):
        return (rec.get("boardAfter") or {}).get("battlefield") or []

    def add(c, sink):
        d = db.get(c.get("cardName"))
        if not d:
            soft.append("unknown-source:" + str(c.get("cardName")))
            return
        u = _units_for(d)
        if u is None:
            return                                        # tapped for something that is not mana
        sink += u[0]
        if u[1]:
            # A spent filter land's own activation cost. Kept OUT of `bill` -- the bill is what the
            # SPELLS cost, and the availability test prices filters itself; folding it into both is
            # how gi1370 came out one pip short of its own sources.
            fcost.append(u[1])

    for i, rec in enumerate(recs):
        if precombat_only and (rec.get("phase") or "").startswith("COMBAT"):
            break
        if not (rec.get("phase") or "").startswith("MAIN"):
            continue
        prev_by, now_by = {}, {}
        for c in (board(recs[i - 1]) if i else []):
            prev_by.setdefault(c.get("card"), c)
        for c in board(rec):
            now_by.setdefault(c.get("card"), c)
        for num, c in now_by.items():
            was = prev_by.get(num)
            if not c.get("tapped"):
                continue
            if was is not None and not was.get("tapped"):
                add(c, units)                             # untapped -> tapped inside this main
            elif was is None:                             # played and tapped in the same main
                d = db.get(c.get("cardName"))
                if d and not _enters_tapped_always(d):
                    add(c, units)
                elif d:
                    # An always-tapped land (a Karoo) makes no mana the turn it lands -- but this
                    # ledger cannot see WHICH copy of a repeated land was played, so book it as a
                    # `maybe`. Over-crediting here can only hide a laundered turn, never invent one.
                    add(c, maybe)
        for num, c in prev_by.items():
            if num in now_by or c.get("tapped"):
                continue
            d = db.get(c.get("cardName")) or {}
            if (d.get("parameters") or {}).get("sac_for_mana_amount"):
                add(c, units)                             # a Treasure vanishes BECAUSE it paid
            else:
                add(c, maybe)                             # bounced / self-sacrificed: probably paid
        # UNTAP_SOURCES (MTG_SPASM_UNTAP_LITERAL): the engine recorded EXACTLY which mana sources
        # Reality Spasm untapped, so each becomes producible once more in its OWN colours. This is
        # an exact credit, not a generous one, so it carries NO `soft` note -- a turn that balances
        # under it is genuinely proven and may classify LEGAL. It also repairs the tapped-delta's
        # blindness in both directions: a source that re-tapped after the untap reads as ONE delta
        # unit though it produced twice (+1 here), and one that stayed untapped reads as ZERO
        # though it produced once before the untap (+1 here).
        rec_untap_events = [a for a in rec.get("actions", []) if a.get("type") == "UNTAP_SOURCES"]
        for a in rec_untap_events:
            for c in a.get("cards", []):
                add(c, units)
        for a in rec.get("actions", []):
            if a.get("type") == "ABILITY":
                # Only a MANA source's tap can mislead this ledger (it would book mana the source
                # never made). A Sliver's pump ability or an Aether Vial activation logs here too and
                # is harmless -- voiding on those alone cost 32 otherwise-priceable turns.
                if _units_for(db.get(a.get("cardName"), {})):
                    hard.append("tap-ability:" + str(a.get("cardName")))
                continue
            if a.get("type") != "CAST_SPELL":
                continue
            # `manaPaid` is `effective.ToString()` (AIEngine.cpp) -- the cost AFTER discounts, i.e.
            # what was really paid. That makes it trustworthy for X spells too: Crackle with Power's
            # {X}{X}{X}{R}{R} at X=2, discounted by Hinata, logs as the {4}{R}{R} it actually cost.
            # The ONE unsafe shape is a literal `{X}` surviving into the string, which means the X
            # was never folded in -- `_parse_cost` rejects that and it lands in `hard` below.
            paid = a.get("manaPaid") or ""
            if paid == "Vial":
                continue                          # Aether Vial: onto the battlefield, no mana paid
            paid = paid.replace(" (retrace)", "")
            got, ok = _parse_cost(paid)
            if not ok:
                hard.append("uncosted:%s(%s)" % (a.get("cardName"), paid))
            printed = (db.get(a.get("cardName")) or {}).get("mana_cost") or ""
            if "/" in printed:
                # ToString() renders a hybrid pip as its FIRST colour (Card.h says so, deliberately,
                # to keep play digests stable). Reading {G/U} as a hard {G} would demand a colour the
                # spell never required -- the one way this ledger could invent a LAUNDERED verdict.
                # Recoverable when nothing discounted the spell: take the PRINTED pips instead, which
                # carry the hybrid, and only void when the two disagree on size.
                pgot, pok = _parse_cost(printed)
                if pok and len(pgot) == len(got):
                    got = pgot
                else:
                    hard.append("hybrid-cost:" + str(a.get("cardName")))
            bill += got
            p = (db.get(a.get("cardName")) or {}).get("parameters") or {}
            if p.get("untap_x_mana_sources"):
                # Under the literal model the UNTAP_SOURCES events above already credited this
                # cast's real untap EXACTLY -- adding the generous chosenX credit on top would
                # double-count. Only fall back to it for a float-model log (no events recorded).
                if rec_untap_events:
                    continue
                # Untapping X sources lets them tap TWICE in one main, so the source count is short
                # by X. Which X is not recorded, but `chosenX` is -- so credit X units able to make
                # any colour this board produces. That is GENEROUS on purpose: over-crediting can
                # only hide a laundered turn (a miss), never manufacture an accusation.
                n = a.get("chosenX")
                if n is None:
                    hard.append("untap-refloat:" + a.get("cardName", "?"))
                else:
                    units += [_board_colors(rec, db)] * int(n)
                    soft.append("untap-refloat:%s(X=%s, credited generously)"
                                % (a.get("cardName", "?"), n))
            if p.get("ritual_floating_mana"):
                col = p.get("ritual_float_color")
                if col:
                    units += [frozenset(col)] * int(p["ritual_floating_mana"])
                else:
                    soft.append("uncoloured-ritual:" + a.get("cardName", "?"))
    return units, maybe, bill, fcost, soft, hard


def _board_colors(rec, db):
    """Every colour the player's own permanents can produce, as one pip set. Used to price an
    untap-refloat: it is an upper bound on what the untapped sources could have made."""
    cols = set()
    for c in ((rec.get("boardAfter") or {}).get("battlefield") or []):
        u = _units_for(db.get(c.get("cardName"), {}))
        if u:
            for s in u[0]:
                cols |= set(s)
    return frozenset(cols or ALL_COLORS)


def _pre_main(game, turn):
    """The record just before the turn's first main -- the board as the main phase opens."""
    recs = [t for t in game.get("turns", []) if t.get("turn") == turn]
    pre = None
    for rec in recs:
        if (rec.get("phase") or "").startswith("MAIN"):
            return pre, recs
        pre = rec
    return None, recs


def _no_free_copy(game, turn, names):
    """On the CONTROL's own line, does it reach combat with NO untapped copy of `names` that could
    have attacked anyway? Then no attacker was lost there and the case has no consequence.

    Measured at the end of the last main before combat -- the declare-attackers state. `tapdiff`
    samples earlier (at a claude-play decision, before the final segment pays), so a permanent the
    control taps LATE in its own main reads as spared there; this is the stricter look.

    Two things it must not be fooled by, both of which it was:
      * COPIES. Creature Giving routinely holds two Birds of Paradise, and a name set reads "tapped"
        when one is tapped and the other is free to swing -- not the same board at all.
      * SUMMONING SICKNESS. gi284 casts a second Elvish Mystic on the very turn it taps its first;
        the new one is untapped and cannot attack, so counting it as a free copy said "an attacker
        survived" about a board where none did. Only copies present as the main opened count."""
    pre, recs = _pre_main(game, turn)
    # By CARD NUMBER, not name. gi284's second Elvish Mystic shares the first one's NAME, so a
    # name-keyed "was it already on the battlefield" test waves the summoning-sick copy through.
    present = {c.get("card") for c in ((pre or {}).get("boardAfter", {}).get("battlefield") or [])}
    last = None
    for rec in recs:
        if (rec.get("phase") or "").startswith("COMBAT"):
            break
        last = rec
    if last is None:
        return False
    free = {c["cardName"] for c in ((last.get("boardAfter") or {}).get("battlefield") or [])
            if not c.get("tapped") and c.get("card") in present}
    return all(nm not in free for nm in names)


def _available_pool(game, turn, db, exclude, skip_played=False):
    """Every mana source the player could have tapped before combat, minus `exclude`.

    This is the pool the USER's question names -- "payable without the creature, using only sources
    the control also had" -- and it is deliberately WIDER than what the control actually spent. A
    source the control left untapped was still available, so a bill it covers is a bill the fixed arm
    could have paid without reaching for a creature.

    `skip_played` drops the land played this turn. That land is CONDITIONALLY untapped for the
    duals here (Game Trail wants a reveal, a shockland wants 2 life), and the log does not say which
    branch was taken -- so a verdict that survives without it is one that does not rest on the guess.

    Returns (fixed_units, filters). A filter land is not a plain source: Cascade Bluffs is
    "{U/R}, {T}: add two of U/R", so it is only worth taking if something can FEED it, and taking it
    when nothing can would invent mana. It is returned separately so the payability test can try the
    subsets."""
    pre, recs = _pre_main(game, turn)
    cards = list((pre or {}).get("boardAfter", {}).get("battlefield") or [])
    if not skip_played:
        for rec in recs:                    # lands played inside the main, if they can enter untapped
            if (rec.get("phase") or "").startswith("COMBAT"):
                break
            for a in rec.get("actions", []):
                if a.get("type") == "PLAY_LAND":
                    d = db.get(a.get("cardName"))
                    if d and not _enters_tapped_always(d):
                        cards.append({"cardName": a.get("cardName"), "tapped": False})
    drop = dict.fromkeys(exclude, 0)
    for nm in exclude:
        drop[nm] = sum(1 for c in cards if c.get("cardName") == nm)   # every copy, not just one
    fixed, filters = [], []
    for c in cards:
        nm = c.get("cardName")
        if c.get("tapped") or nm not in db:
            continue
        if drop.get(nm):
            drop[nm] -= 1
            continue
        u = _units_for(db[nm])
        if not u:
            continue
        (filters if u[1] else fixed).append(u)
    return fixed, filters


def _conditional_land_played(game, turn, db):
    """Was the land played this turn one whose ETB-untapped-ness is a CHOICE the log does not
    record? Game Trail (reveal a Mountain/Forest) and the shocklands (pay 2 life) are; a basic, a
    filter land and Reflecting Pool are not -- they always enter untapped."""
    _, recs = _pre_main(game, turn)
    for rec in recs:
        if (rec.get("phase") or "").startswith("COMBAT"):
            break
        for a in rec.get("actions", []):
            if a.get("type") != "PLAY_LAND":
                continue
            p = (db.get(a.get("cardName")) or {}).get("parameters") or {}
            if p.get("etb_untap_reveal_subtypes") or p.get("etb_pay_life_to_untap"):
                return True
    return False


def _played_land_paid(game, turn, db):
    """Did the land played this turn demonstrably PRODUCE mana -- i.e. did it enter untapped?

    Counting settles it without knowing which ETB branch ran. If the pre-combat bill needs more mana
    than every OTHER source the control tapped can supply, the shortfall can only have come from the
    land it played, so that land entered untapped. gi309 is the shape: {G}+{G}+{1}{G} off three
    standing lands plus the Game Trail it played that turn -- four mana, four sources, no slack."""
    pre, recs = _pre_main(game, turn)
    played, bill = [], []
    prev = dict.fromkeys([], 0)
    prev = {c.get("card"): c for c in ((pre or {}).get("boardAfter", {}).get("battlefield") or [])}
    others = 0
    for rec in recs:
        if (rec.get("phase") or "").startswith("COMBAT"):
            break
        if not (rec.get("phase") or "").startswith("MAIN"):
            continue
        for a in rec.get("actions", []):
            if a.get("type") == "PLAY_LAND":
                played.append(a.get("cardName"))
            elif a.get("type") == "CAST_SPELL":
                got, ok = _parse_cost(a.get("manaPaid"))
                if not ok:
                    return False
                bill += got
        for c in ((rec.get("boardAfter") or {}).get("battlefield") or []):
            was = prev.get(c.get("card"))
            if c.get("tapped") and was is not None and not was.get("tapped"):
                u = _units_for(db.get(c.get("cardName"), {}))
                others += len(u[0]) if u else 0
    return bool(played) and others < len(bill)


def _payable_with_filters(pips, fixed, filters):
    """Payable using the fixed units plus any SUBSET of the filter lands (each one adding its units
    and its own activation pip)."""
    flat = [u for f in fixed for u in f[0]]
    for mask in range(1 << min(len(filters), 6)):
        units, extra = list(flat), []
        for i, f in enumerate(filters):
            if mask >> i & 1:
                units += f[0]
                extra += [f[1]] if isinstance(f[1], frozenset) else [ANY_PIP] * f[1]
        if _payable(pips + extra, units):
            return True
    return False


def adjudicate_one(binary, r):
    db = _card_db()
    detail = r.get("tap_detail") or ""
    if not detail:
        return dict(r, verdict="NOT-TAP-ORDER", verdict_detail="")
    tmp = tempfile.mkdtemp(prefix="adjud_")
    try:
        cmd = [binary, os.path.join(ROOT, r["deck"]), "--profile", os.path.join(ROOT, r["profile"]),
               "--games", "1", "--seed", r["seed"], "--game-index", r["game_index"],
               "--depth", r["depth"], "--budget-ms", r["budget"],
               "--ignore-play-profile", "--log-dir", tmp]
        subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True,
                       env=dict(os.environ, **LEGACY_ENV))
        logs = glob.glob(os.path.join(tmp, "*.json"))
        if not logs:
            return dict(r, verdict="ERR:no-control-log", verdict_detail="")
        g = json.load(open(logs[0]))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    res = g.get("result") or {}
    ctl = res.get("turn") if res.get("winner") not in (None, "none") else -1
    if str(ctl) != str(r["pre_fix"]):
        # The control must reproduce the PRE-fix score or it is not the line under dispute.
        return dict(r, verdict="ERR:control-drift", verdict_detail="control=%s pre_fix=%s"
                    % (ctl, r["pre_fix"]))

    # TWO questions, deliberately kept apart -- conflating them is what made the first pass of this
    # checker wrong twice:
    #   Q1 SPEND LEGALITY  was the control's OWN payment colour-legal under true colours? A `no`
    #                      means the pre-fix score came off a laundered payment, so `pre_fix` is not
    #                      automatically the right answer to restore.
    #   Q2 AVOIDABILITY    was the bill payable from everything AVAILABLE, without any copy of the
    #                      disputed creature? A `yes` means the tap was a source-selection choice,
    #                      not a requirement -- which is the defect.
    # They are independent: a turn can launder AND still have had a creature-free legal payment.
    out, worst = [], 0
    RANK = {"DEFECT": 4, "WARRANTED": 3, "CONTROL-ALSO-TAPS": 2, "UNMODELLED": 1}
    for seg in detail.split(";"):
        tn = int(seg.split(":", 1)[0][1:])
        disputed = seg.split(":", 1)[1].split("+")
        units, maybe, bill, fcost, soft, hard = _turn_ledger(g, tn, db)
        fixed, filters = _available_pool(g, tn, db, disputed)
        navail = sum(len(f[0]) for f in fixed) + sum(len(f[0]) for f in filters)
        if _no_free_copy(g, tn, disputed):
            # The signature came from the forced WALK; on the control's own autonomous line -- the
            # one ground truth actually records -- no copy that could have attacked survives to
            # combat either, so nothing was lost here. Reported, not silently folded in.
            v, why = "CONTROL-ALSO-TAPS", "control reaches combat with no free %s" % "+".join(disputed)
        elif hard:
            v, why = "UNMODELLED", ",".join(sorted(set(hard)))
        elif not bill:
            v, why = "UNMODELLED", "no cast before combat -- the tap paid for nothing pre-combat"
        else:
            spend_ok = (_payable(bill + fcost, units)
                        or _payable(bill + fcost, units + maybe))
            avoid_ok = _payable_with_filters(bill, fixed, filters)
            if avoid_ok:
                nf, nfil = _available_pool(g, tn, db, disputed, skip_played=True)
                if (not _payable_with_filters(bill, nf, nfil)
                        and _conditional_land_played(g, tn, db)
                        and not _played_land_paid(g, tn, db)):
                    # Only a CONDITIONALLY untapped land is in doubt (Game Trail wants a reveal, a
                    # shockland 2 life) and the log does not record which branch ran -- unless the
                    # control's own spend proves it, which _played_land_paid tests. A plain land or
                    # an always-untapped dual needs no assumption at all.
                    soft.append("needs-the-land-played-this-turn")
            n = "%d pips, %d sources free of %s" % (len(bill), navail, "+".join(disputed))
            if avoid_ok and not soft:
                v = "DEFECT"
                why = n + (", colour-legal" if spend_ok
                           else ", colour-legal -- though the control's OWN payment was NOT "
                                "(it laundered, so pre_fix is not automatically the right target)")
            elif avoid_ok:
                v, why = "DEFECT", n + ", colour-legal (units under-counted: %s -- only helps)" \
                                       % ",".join(sorted(set(soft)))
            elif soft:
                v, why = "UNMODELLED", ",".join(sorted(set(soft)))
            else:
                v, why = "WARRANTED", n + " cannot cover the bill -- a creature was genuinely needed"
        out.append("T%d=%s(%s)" % (tn, v, why))
        worst = max(worst, RANK[v])
    verdict = {v: k for k, v in RANK.items()}.get(worst, "UNMODELLED")
    return dict(r, verdict=verdict, verdict_detail="; ".join(out))


# ---------------------------------------------------------------- legality
# `adjudicate` settles the 25 games that carry the tap-order signature. The other 393 do not, and
# "no signature" is evidence they are not THIS defect -- not evidence the regression is right. There
# is one more question the same data answers for every case in the set, tap-order or not:
#
#     was the PRE-FIX line's own payment colour-legal, under true colours, on every turn?
#
# A `no` means the recorded `pre_fix` came off a payment the rules do not allow, so the game getting
# worse is the fix working and `pre_fix` is not a number to restore. A `yes` means the old line was
# legitimate and something else moved -- which is the case that deserves a look.
#
# The strong/weak distinction matters. If the sources the control tapped are FEWER than the mana it
# spent, this model is missing production and says nothing (UNPRICED). Only a failure with enough
# total mana on the table is a colour failure, and a colour failure IS laundering.
def legality_one(binary, r):
    db = _card_db()
    tmp = tempfile.mkdtemp(prefix="legal_")
    try:
        cmd = [binary, os.path.join(ROOT, r["deck"]), "--profile", os.path.join(ROOT, r["profile"]),
               "--games", "1", "--seed", r["seed"], "--game-index", r["game_index"],
               "--depth", r["depth"], "--budget-ms", r["budget"],
               "--ignore-play-profile", "--log-dir", tmp]
        subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True,
                       env=dict(os.environ, **LEGACY_ENV))
        logs = glob.glob(os.path.join(tmp, "*.json"))
        if not logs:
            return dict(r, spend="ERR:no-control-log", spend_detail="")
        g = json.load(open(logs[0]))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    bad, unpriced = [], []
    for t in sorted({x.get("turn") for x in g.get("turns", []) if x.get("turn")}):
        units, maybe, bill, fcost, soft, hard = _turn_ledger(g, t, db, precombat_only=False)
        if not bill:
            continue
        if hard:
            unpriced.append("T%d:%s" % (t, ",".join(sorted(set(hard)))))
            continue
        pool = units + maybe
        want = bill + fcost
        if _payable(want, pool):
            # A `soft` note means the pool was credited GENEROUSLY (an untap-refloat priced at the
            # board's whole colour range). Balancing under that credit does not prove legality, so it
            # cannot be reported as LEGAL -- but failing under it is all the stronger.
            if soft:
                unpriced.append("T%d:%s" % (t, ",".join(sorted(set(soft)))))
            continue
        if len(pool) < len(want):
            unpriced.append("T%d:short(%d<%d)" % (t, len(pool), len(want)))
        else:
            bad.append("T%d:%d mana on %d sources, colours do not match%s"
                       % (t, len(want), len(pool), " (even generously credited)" if soft else ""))
    if bad:
        return dict(r, spend="LAUNDERED", spend_detail="; ".join(bad))
    if unpriced:
        return dict(r, spend="UNPRICED", spend_detail="; ".join(unpriced))
    return dict(r, spend="LEGAL", spend_detail="")


def cmd_legality(args):
    binary = args.bin or os.path.join(ROOT, "build/Release/mtg")
    rows = read_cases(args.filter)
    print(f"LEGALITY  {len(rows)} cases -- was the PRE-FIX line's own payment colour-legal?")
    print("  LAUNDERED = a turn spends more of a colour than its sources make: pre_fix came off an")
    print("              illegal payment, so the game getting worse is the fix WORKING")
    print("  LEGAL     = every turn balances under true colours -- the old line was legitimate")
    print("  UNPRICED  = a turn this model refuses to price ({X}, untap-refloat, missing source)\n")
    done, agg = [], {}
    with futures.ThreadPoolExecutor(max_workers=args.jobs) as ex:
        for a in ex.map(lambda r: legality_one(binary, r), rows):
            done.append(a)
            agg[a["spend"]] = agg.get(a["spend"], 0) + 1
            if a["spend"] == "LAUNDERED":
                print(f"  {a['key']} gi{a['gi']}: {a['pre_fix']}->{a['post_fix']} "
                      f"[{a['attribution']}] {a['spend_detail']}", flush=True)
    print("\nsummary: " + ", ".join(f"{k}={v}" for k, v in sorted(agg.items())))
    cross = {}
    for a in done:
        cross.setdefault(a["attribution"], {}).setdefault(a["spend"], 0)
        cross[a["attribution"]][a["spend"]] += 1
    print("\nby attribution:")
    for k in sorted(cross):
        print(f"  {k:20s} " + ", ".join(f"{x}={y}" for x, y in sorted(cross[k].items())))
    with open(os.path.join(ROOT, "logs", "prepay_legality.json"), "w") as fh:
        json.dump(done, fh, indent=1)
    print("\ndetail -> logs/prepay_legality.json")
    _write_back({(r["key"], str(r["gi"])): {"spend": r["spend"]} for r in done})
    return 0


def _write_back(updates):
    """Merge per-case columns into the COMMITTED tsv. logs/ is gitignored, and the tsv is the
    artifact another tree actually receives."""
    rows = read_cases()
    with open(CASES, "w") as fh:
        fh.write("# Games the 2026-08-25 colour-honesty batch made WORSE.\n")
        fh.write("# pre_fix = win turn before the batch, post_fix = win turn now (-1 = no win).\n")
        fh.write("# verdict: DEFECT   = the turn is PROVEN payable without the creature the engine\n")
        fh.write("#                     taps  (section 2c of docs/design/prepay-payment-path-recheck.md)\n")
        fh.write("# spend:   LAUNDERED= the PRE-FIX line paid a pip its own sources could not make,\n")
        fh.write("#                     so pre_fix is not a number to restore   (section 2d)\n")
        fh.write("# " + "\t".join(COLS) + "\n")
        for r in rows:
            r.update(updates.get((r["key"], str(r["gi"])), {}))
            fh.write("\t".join(str(r.get(c, "")) for c in COLS) + "\n")
    print(f"columns -> {os.path.relpath(CASES, ROOT)}")


def cmd_adjudicate(args):
    binary = args.bin or os.path.join(ROOT, "build/Release/mtg")
    src = os.path.join(ROOT, "logs", "prepay_tapdiff.json")
    if not os.path.exists(src):
        print("run `tapdiff` first -- adjudicate reads logs/prepay_tapdiff.json")
        return 1
    rows = [r for r in json.load(open(src)) if r.get("tap_class") == "TAP-ORDER"]
    if args.filter:
        rows = [r for r in rows if args.filter in r["key"]]
    print(f"ADJUDICATE  {len(rows)} TAP-ORDER cases -- was the bill payable WITHOUT the creature?")
    print("  DEFECT            = yes, from sources the control also had: the tap was a CHOICE")
    print("  WARRANTED         = no: a creature was genuinely needed, so the fix is right")
    print("  CONTROL-ALSO-TAPS = the control reaches combat without that attacker either -- no loss")
    print("  UNMODELLED        = the ledger contains something this checker refuses to price\n")
    done, agg = [], {}
    with futures.ThreadPoolExecutor(max_workers=args.jobs) as ex:
        for a in ex.map(lambda r: adjudicate_one(binary, r), rows):
            done.append(a)
            agg[a["verdict"]] = agg.get(a["verdict"], 0) + 1
            print(f"  {a['key']} gi{a['gi']}: {a['pre_fix']}->{a['post_fix']}  "
                  f"{a['verdict']:10s} {a['verdict_detail']}", flush=True)
    print("\nsummary: " + ", ".join(f"{k}={v}" for k, v in sorted(agg.items())))
    with open(os.path.join(ROOT, "logs", "prepay_adjudicate.json"), "w") as fh:
        json.dump(done, fh, indent=1)
    print("detail -> logs/prepay_adjudicate.json")
    # ... and back into the COMMITTED case list, because logs/ is gitignored and the tsv is the
    # artifact another tree actually gets. `verify --defects` then narrows to the proven set.
    _write_back({(r["key"], str(r["gi"])): {"verdict": r["verdict"]} for r in done})
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build");    b.add_argument("--baseline", default="90a537bd")
    v = sub.add_parser("verify")
    c = sub.add_parser("classify")
    t = sub.add_parser("tapdiff")
    j = sub.add_parser("adjudicate")
    g = sub.add_parser("legality")
    v.add_argument("--defects", action="store_true",
                   help="only the games section 2c PROVED were payable without the creature")
    for p in (v, c, t, j, g):
        p.add_argument("--jobs", type=int, default=min(16, (os.cpu_count() or 4) - 2))
        p.add_argument("--bin", default=None)
        p.add_argument("--filter", default=None)
    a = ap.parse_args()
    return {"build": cmd_build, "verify": cmd_verify, "classify": cmd_classify,
            "tapdiff": cmd_tapdiff, "adjudicate": cmd_adjudicate,
            "legality": cmd_legality}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())

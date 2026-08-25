#!/usr/bin/env python3
"""Is the GROUND-TRUTH line of a changed game still PLAYABLE under the current engine?

`audit_changed_games.py` says a game moved; `explain_game.py` shows the old and new lines side by
side; `classify_turn_later.sh` says whether a slowdown recovers at higher budget. None of them
answers the question that decides a rebaseline after a RULES fix:

    the engine used to play line L and now plays something worse -- is L still legal?

  * L is now ILLEGAL  -> the old ground truth recorded a line the rules do not allow. The score got
    worse because the engine stopped cheating. Rebaseline.
  * L is still LEGAL  -> the engine had a legal, better line available and did not take it. That is
    a regression (or budget churn), NOT something to rebaseline over.

Method: replay the OLD game's own line, turn by turn, against the CURRENT binary and let the engine
rule on it. The old line comes from the baseline binary's own game log (logs/snapshots/<mode>-baseline,
saved by `regression.sh --accept`; --old-bin overrides). Each turn's land+casts are handed to
`--validate-line`, which runs TurnSolver::CheckLine -- the same path the play viewer uses -- and
returns one of:

    accept / choose        the line is playable; the walk takes it and moves on
    illegal                the engine says the rules forbid it  <-- the proof we are after
    legal_not_enumerated   legal, but the search no longer offers it (a PRUNING change, not a
                           legality one -- reported separately, because it does NOT justify a
                           rebaseline on rules grounds)

Two properties make the verdict trustworthy in the direction we need it:
  * `--claude-play` enumerates a SUPERSET of autonomous play (human-play widenings only ever add
    options), so "illegal here" implies "illegal in the autonomous run" too;
  * the walk forces the recorded opening hand (--force-mulligan) and steers resolution-time target
    prompts to the old line's own targets, so a divergence is about the LINE, not the deal.

Usage:
    python3 test/gt_line_playable.py <mode> <key> <gi> [<gi> ...]
    python3 test/gt_line_playable.py <mode> --slower          # every SEARCHED slower game
    python3 test/gt_line_playable.py <mode> --slower --d0     # ... and the d0 ones
    python3 test/gt_line_playable.py <mode> --slower --jobs 8 # parallel (each game is serial)
    [--old-bin P] [--new-bin P]
"""
import concurrent.futures as futures
import glob, importlib.util, json, os, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

_spec = importlib.util.spec_from_file_location("explain", os.path.join(HERE, "explain_game.py"))
explain = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(explain)

DEC_RE = re.compile(r"<<<CLAUDE_DECISION>>>\s*(\{.*?\})\s*<<<END_(?:CLAUDE_)?DECISION>>>", re.S)
VAL_RE = re.compile(r"<<<CLAUDE_VALIDATION>>>\s*(\{.*?\})\s*<<<END_(?:CLAUDE_)?VALIDATION>>>", re.S)
RES_RE = re.compile(r"<<<CLAUDE_RESULT>>>\s*(\{.*?\})\s*<<<END_(?:CLAUDE_)?RESULT>>>", re.S)


# ---- the OLD line, from the CURRENT binary with the fixes switched off ------------------------
def old_game(binary, r, gi, env_extra=None):
    """Run one game under `binary` and return (per-turn ordered actions, mulligan spec, win turn).

    NOT the pre-fix BINARY. `src/cards/data/cards.json` is read at RUNTIME (--cards-json, default
    relative to the CWD), so a baseline binary invoked from the working tree silently loads the
    CURRENT card data -- and one of this batch's fixes IS card data (Irencrag Feat's
    ritual_float_color). Running an old build from the new tree therefore reproduces neither engine:
    it measured Dragonstorm gi171 at T7 where the ground truth it supposedly produced says T5.
    LEGACY_ENV avoids the trap entirely: one binary, one card file, the fixes switched off -- and it
    reproduces the recorded score on every game checked."""
    tmp = tempfile.mkdtemp(prefix="gtline_")
    try:
        cmd = [binary, os.path.join(ROOT, r["deckfile"]),
               "--profile", os.path.join(ROOT, r["profile"]), "--games", "1",
               "--seed", str(r["seed"] + gi), "--game-index", str(gi),
               "--depth", str(r["depth"]), "--budget-ms", str(r["budget"]),
               "--ignore-play-profile", "--log-dir", tmp]
        subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True,
                       env=dict(os.environ, **(env_extra or {})))
        logs = glob.glob(os.path.join(tmp, "*.json"))
        if not logs:
            return None
        d = json.load(open(logs[0]))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    turns = {}
    draws, sig = [], {}
    for t in d.get("turns", []):
        tn = t.get("turn")
        e = turns.setdefault(tn, [])
        # The full per-turn PLAY signature, not just the cast list. `manaPaid` is what separates a
        # Crackle with Power for X=3 from the same card for X=7, and REVEAL/ATTACK are decisions
        # too; comparing names alone reported "no play divergence" on games whose draws plainly
        # diverged, which is how draw divergence came to look like exogenous variance.
        g = sig.setdefault(tn, [])
        ba = t.get("boardAfter") or {}
        if "opponentLife" in ba:
            g.append(("LIFE", None, str(ba["opponentLife"]), ()))
        for a in t.get("actions", []):
            ty = a.get("type")
            tg = tuple(x.get("cardName", "?") for x in (a.get("targets") or []))
            if ty == "DRAW":
                draws.append((tn, a.get("cardName", "?")))
                continue
            g.append((ty, a.get("cardName"), a.get("manaPaid"), tg))
            if ty == "PLAY_LAND":
                e.append(("land", a.get("cardName", "?"), None))
            elif ty == "CAST_SPELL":
                e.append(("cast", a.get("cardName", "?"), tg[0] if tg else None))
    # --force-mulligan "<count>:<bottomed nums>": the recorded keep, so the replay deals the same
    # seven cards no matter what the current keep/bottom heuristic would do with them.
    seq = d.get("mulliganSequence") or []
    kept = next((s for s in seq if s.get("kept")), None)
    count = (kept.get("attempt") if kept else len(seq) - 1) or 0
    opening = {c.get("card") for c in (d.get("openingHand") or [])}
    bottom = [c.get("card") for c in ((kept or {}).get("hand") or []) if c.get("card") not in opening]
    res = d.get("result") or {}
    win = res.get("turn") if res.get("winner") not in (None, "none") else None
    return (turns, f"{count}:" + ",".join(str(n) for n in bottom), win, draws, sig,
            tuple(sorted(c.get("cardName", "?") for c in (d.get("openingHand") or []))))


# ---- drive the CURRENT binary down that line -------------------------------------------------
def spec_of(actions):
    """LineSpec text for a list of (kind, name, target) -- 'land=X;cast=A;cast=B'."""
    return ";".join(("land=" if k == "land" else "cast=") + nm for k, nm, _ in actions)


# Every fix in this batch, switched OFF. Running the CURRENT binary with these set reproduces the
# pre-fix engine in the SAME executable -- which is what makes a refusal attributable: if the line
# plays with them and not without, the fixes are the reason, and no other difference between two
# builds can be. (MTG_LEGACY_* re-enable rules violations; they exist for exactly this A/B.)
LEGACY_ENV = {"MTG_PREPAY_TRUE_COLOURS": "0", "MTG_LEGACY_RITUAL_WILD": "1",
              "MTG_LEGACY_STAGED_SUSPEND": "1", "MTG_AURA_RANK_MODE": "1",
              "MTG_NO_LINE_HOLD": "1", "MTG_LEGACY_BESTOW_SIG": "1"}


def play_step(bin_, r, gi, force, choices, validate=None, max_turns=None, env_extra=None):
    args = [bin_, os.path.join(ROOT, r["deckfile"]), "--claude-play",
            "--profile", os.path.join(ROOT, r["profile"]),
            "--seed", str(r["seed"] + gi), "--game-index", str(gi),
            "--depth", str(r["depth"]), "--budget-ms", str(r["budget"]),
            "--ignore-play-profile", "--force-mulligan", force]
    if max_turns is not None:
        args += ["--max-turns", str(max_turns)]
    if choices:
        args += ["--choices", ",".join(str(c) for c in choices)]
    if validate is not None:
        args += ["--validate-line", validate]
    env = dict(os.environ, MTG_PLAY_PLANS_CAP="0", **(env_extra or {}))
    p = subprocess.run(args, cwd=ROOT, capture_output=True, text=True, env=env)
    return p.returncode, p.stdout + p.stderr


VERBOSE = False


def vlog(msg):
    if VERBOSE:
        print("      | " + msg, flush=True)


def walk_line(bin_, r, gi, force, turns, hints=None, capture=None, tag="", env_extra=None):
    """Drive `bin_` down the recorded `turns` line, one --validate-line per segment.

    hints:   {(turn, source): label} -- how to answer a resolution-time `target` prompt. Recovering
             these from the OLD binary's own walk is what makes the forced replay comparable: the
             old game log records WHICH spells were cast but not what a trick targeted (the target
             used to ride the plan as `enchant_target`, and only the plan JSON carries its name).
             Without them the replay defaults every trick and a Zada fan-out silently points
             somewhere else -- the line would be identical and the outcome still differ.
    capture: same dict, filled in as this walk goes (plan-carried enchant targets + answered
             target prompts).
    """
    choices, refused, notes = [], {}, []
    pending = {tn: list(acts) for tn, acts in turns.items()}
    for _ in range(400):
        rc, out = play_step(bin_, r, gi, force, choices, env_extra=env_extra)
        if rc == 0:
            m = RES_RE.search(out)
            res = json.loads(m.group(1)) if m else {}
            rest = {t: [f"{k}:{n}" for k, n, _ in a] for t, a in pending.items() if a}
            return dict(ok=True, unplayed=rest, refused=refused, notes=notes,
                        win=res.get("win_turn") if res.get("won") else None)
        m = DEC_RE.search(out)
        if not m:
            return dict(ok=False, detail=out.strip()[-200:], refused=refused, notes=notes)
        dec = json.loads(m.group(1))
        if dec.get("type") != "main_phase":
            a = answer_aux(dec, hints)
            if capture is not None:
                capture.append((aux_ident(dec), opt_key(dec, a), a))
            vlog(f"{tag}T{dec.get('turn')} {dec.get('type')}({dec.get('source')}) <- {a}")
            choices.append(a)
            continue

        turn = dec.get("turn")
        vlog(f"{tag}T{turn} {dec.get('phase')} hand="
             + ",".join(sorted(c.get("name", "") for c in dec.get("me", {}).get("hand", []))))
        want = pending.get(turn) or []
        if not want:
            choices.append(-1)                     # nothing of the old line left this turn: pass
            continue
        # Longest playable prefix of what this turn still owes. The old line is one turn's worth of
        # actions; the engine may need more than one segment to express it, so shrinking from the
        # full set finds the split rather than declaring a legal line illegal.
        taken = None
        for cut in range(len(want), 0, -1):
            sub = want[:cut]
            rc2, out2 = play_step(bin_, r, gi, force, choices, validate=spec_of(sub),
                                  env_extra=env_extra)
            mv = VAL_RE.search(out2)
            if not mv:
                continue
            v = json.loads(mv.group(1))
            vlog(f"{tag}  try {spec_of(sub)} -> {v.get('verdict')}"
                 + (f" [{v.get('reason')}]" if v.get("verdict") not in ("accept", "choose") else ""))
            if v.get("verdict") in ("accept", "choose"):
                idx = v.get("plan_index")
                if idx is None or idx < 0:
                    vs = v.get("variants") or []
                    idx = vs[0]["plan_index"] if vs else None
                if idx is None or idx < 0:
                    continue
                taken = (cut, idx)
                if capture is not None:
                    # A trick's target used to ride the PLAN (enchant_target) rather than a
                    # resolution prompt, so record it here too and let the replay's `target` frame
                    # pick it up by name.
                    plan = next((p for p in (dec.get("plans") or []) if p.get("index") == idx), None)
                    for a in ((plan or {}).get("actions") or []):
                        if a.get("enchant_target_name"):
                            capture.append((("target", turn, a.get("card")),
                                            ("label", a["enchant_target_name"]), None))
                break
            if cut == len(want) and turn not in refused:
                # The engine's own reason for refusing the whole remaining line, kept in case this
                # turn ends up never fully played (see the terminal branch).
                refused[turn] = dict(turn=turn, phase=dec.get("phase"), spec=spec_of(sub),
                                     verdict=v.get("verdict"), reason=v.get("reason"),
                                     failed_action=v.get("failed_action"))
        if taken is None:
            choices.append(-1)                     # engine will not take any of it: pass this frame
            if pending.get(turn):
                notes.append(f"T{turn}: refused {spec_of(want)}")
                pending[turn] = []                 # do not retry the same frame forever
            continue
        cut, idx = taken
        choices.append(idx)
        pending[turn] = want[cut:]
    return dict(ok=False, detail="no terminal in 400 decisions", refused=refused, notes=notes)


def aux_ident(dec):
    """WHAT a non-main frame is, for aligning the control walk's answers onto the replay."""
    return (dec.get("type"), dec.get("turn"), dec.get("source"))


def opt_key(dec, idx):
    """Stable CONTENT of the option `idx`, so the replay can pick the same one when the option
    ORDER moved. Same three shapes viewer_protocol_check keys on: library placements, card names,
    target labels."""
    o = next((x for x in (dec.get("options") or []) if x.get("index") == idx), None)
    if not isinstance(o, dict):
        return None
    if "top" in o or "away" in o or "shuffle" in o:
        return ("placement", tuple(o.get("top") or []), tuple(o.get("away") or []),
                bool(o.get("shuffle")))
    if "name" in o:
        return ("name", o.get("name"))
    if isinstance(o.get("label"), str):
        return ("label", o["label"])
    return None


def check_game(mode, key, gi, old_bin, new_bin):
    """Walk the old line against the current binary. Returns a dict verdict."""
    cfg = explain.load_cases()
    r = explain.resolve(mode, key, cfg)
    og = old_game(new_bin, r, gi, LEGACY_ENV)
    if og is None:
        return dict(key=key, gi=gi, status="NO-OLD-LOG")
    turns, force, old_win, old_draws = og[0], og[1], og[2], og[3]
    ng = old_game(new_bin, r, gi)                      # the CURRENT autonomous game, for the draws
    new_draws = ng[3] if ng else []
    diverge = next((t for (t, c), (t2, c2) in zip(old_draws, new_draws) if c != c2), None)
    if diverge is None and len(old_draws) != len(new_draws):
        diverge = (old_draws[min(len(new_draws), len(old_draws) - 1)][0] if old_draws else None)

    # Pass 1 -- the CONTROL: the CURRENT binary with every fix in this batch switched OFF
    # (LEGACY_ENV). Same executable, same harness, same forced line -- the only difference is the
    # fixes. That is what makes a refusal in pass 2 attributable: forcing a line re-derives every
    # sub-decision (a scry placement, a trick's target) and a defaulted scry puts a different card
    # on top, so the walk can drift off the recorded line and the engine then rightly says
    # "'Ponder' is not in hand". That reads exactly like a legality finding and is not one. Only a
    # refusal the control does NOT reproduce is evidence.
    ctrl_caps = []
    ctrl = walk_line(new_bin, r, gi, force, turns, capture=ctrl_caps, tag="ctl ",
                     env_extra=LEGACY_ENV)
    if not ctrl.get("ok"):
        return dict(key=key, gi=gi, status="PROTOCOL-BREAK", detail=ctrl.get("detail"))
    ctrl_refused, ctrl_unplayed = ctrl.get("refused") or {}, ctrl.get("unplayed") or {}

    # Pass 2 -- the same binary with the fixes ON, replaying the control's own sub-decisions.
    w = walk_line(new_bin, r, gi, force, turns, hints=ctrl_caps, tag="new ")
    if not w.get("ok"):
        return dict(key=key, gi=gi, status="PROTOCOL-BREAK", detail=w.get("detail"))
    rest = w["unplayed"]
    # A refusal only MATTERS if that turn's actions were never played. The engine often needs two
    # segments for one recorded turn (a cantrip mid-line draws the next card the line casts), so the
    # whole-turn spec is legitimately refused and the split then plays in full -- reporting that as
    # "illegal" would be a method artifact, not a finding.
    blocked = next((w["refused"][t] for t in sorted(rest) if t in w["refused"]), None)
    ctrl_blocked = next((ctrl_refused[t] for t in sorted(ctrl_unplayed) if t in ctrl_refused), None)
    return dict(key=key, gi=gi, status="WALKED", old_win=old_win, old_self=ctrl.get("win"),
                replay_win=w["win"], unplayed=rest, blocked=blocked, notes=w["notes"],
                ctrl_unplayed=ctrl_unplayed, ctrl_blocked=ctrl_blocked, diverge=diverge)


def answer_aux(dec, hints):
    """Answer a non-main frame. A resolution-time `target` prompt is steered to the OLD line's own
    target for that source (so a re-targeted trick is not mistaken for a changed line); everything
    else takes the engine's own default, exactly as an unattended run would.

    `hints` is the CONTROL walk's answer list (see check_game): matched on frame identity and
    replayed by option CONTENT, never by index. Answering these from defaults instead is what let a
    defaulted Ponder scry re-order the library and make the next turn's recorded cast "not in hand"
    -- a divergence that reads exactly like an illegal line."""
    if hints:
        ident = aux_ident(dec)
        for i, (fid, key, idx) in enumerate(hints):
            if fid != ident or key is None:
                continue
            del hints[i]                     # each recorded answer is consumed once, in order
            for o in dec.get("options") or []:
                if opt_key(dec, o.get("index")) == key:
                    return o.get("index", 0)
            if key[0] == "label":            # label with a volatile "(2/2, yours)" tail
                base = key[1].split(" (")[0]
                for o in dec.get("options") or []:
                    if (o.get("label") or "").startswith(base):
                        return o.get("index", 0)
            break
    hd = dec.get("heuristic_default")
    if isinstance(hd, int):
        return hd
    ac = dec.get("ai_choice")
    if isinstance(ac, int):
        return ac
    if isinstance(ac, dict) and isinstance(ac.get("index"), int):
        return ac["index"]
    return 0


# ---- which fix moved this game -----------------------------------------------------------------
SWITCHES = [("prepay-colours",  {"MTG_PREPAY_TRUE_COLOURS": "0"}),
            ("ritual-colours",  {"MTG_LEGACY_RITUAL_WILD": "1"}),
            ("staged-suspend",  {"MTG_LEGACY_STAGED_SUSPEND": "1"}),
            ("aura-fetch-order", {"MTG_AURA_RANK_MODE": "1"}),
            ("line-hold",       {"MTG_NO_LINE_HOLD": "1"}),
            # Added after the first overnight audit came back with FOUR Minotaur games reading
            # "(none - not moved by this batch)". They were moved by the bestow #B0/#B1 plan-signature
            # split, which is the batch's only change to the AUTONOMOUS search space and originally
            # shipped without a switch -- so there was nothing for this bisect to flip. An unswitched
            # lever is invisible here, which is a good reason for the convention that every lever has one.
            ("bestow-signature", {"MTG_LEGACY_BESTOW_SIG": "1"})]


def score_once(bin_, r, gi, env_extra=None):
    """The loss-penalized score of ONE autonomous game (not the forced walk) -- the same number the
    ground truth records."""
    cmd = [bin_, os.path.join(ROOT, r["deckfile"]),
           "--profile", os.path.join(ROOT, r["profile"]), "--games", "1",
           "--seed", str(r["seed"] + gi), "--game-index", str(gi),
           "--depth", str(r["depth"]), "--budget-ms", str(r["budget"]), "--ignore-play-profile"]
    env = dict(os.environ, **(env_extra or {}))
    out = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, env=env).stdout
    m = re.search(r"avg \(turns\)\s*:\s*([0-9.]+)", out)
    return float(m.group(1)) if m else None


def attribute(mode, key, gi, new_bin):
    """Name the fix responsible for a moved game: flip ONE switch at a time on the CURRENT binary
    and see which restores the old score. Autonomous, so this is the score the GT actually records
    -- unlike the forced walk, which answers legality. `all` is the sanity check: with every fix off
    the current binary must reproduce the old number, else something outside this batch moved."""
    cfg = explain.load_cases()
    r = explain.resolve(mode, key, cfg)
    now = score_once(new_bin, r, gi)
    allo = score_once(new_bin, r, gi, LEGACY_ENV)
    hits = []
    if allo is not None and now is not None and allo != now:
        for name, env in SWITCHES:
            if score_once(new_bin, r, gi, env) == allo:
                hits.append(name)
    return dict(key=key, gi=gi, now=now, fixes_off=allo, caused_by=hits)


# ---- the FIRST divergent decision --------------------------------------------------------------
def first_divergence(mode, key, gi, new_bin):
    """Where the two engines first PLAY differently -- and whether the old choice was still legal.

    "The draws diverged" is not a cause. The library is a fixed permutation of the seed, so a
    different card can only be drawn after a different PLAY: a cantrip that did or did not resolve,
    a scry/shuffle that reordered the library, a turn that did or did not happen. Reporting draw
    divergence as if it were exogenous variance hides the only question worth asking -- which
    decision changed, and was the old one still available?

    Forcing only the IDENTICAL PREFIX is also what makes the verdict trustworthy. Walking a whole
    game re-derives every sub-decision from defaults and accumulates drift, which is what produced
    the "'Ponder' is not in hand" false findings. Up to the first divergent turn both engines played
    the same line by construction, so there is nothing to drift.
    """
    cfg = explain.load_cases()
    r = explain.resolve(mode, key, cfg)
    old = old_game(new_bin, r, gi, LEGACY_ENV)
    new = old_game(new_bin, r, gi)
    if old is None or new is None:
        return dict(key=key, gi=gi, status="NO-LOG")
    oturns, force, old_win, odraws, osig, ohand = old
    nturns, _, new_win, ndraws, nsig, nhand = new
    if ohand != nhand:
        return dict(key=key, gi=gi, status="MULLIGAN-DIVERGENCE", old_win=old_win, new_win=new_win,
                    detail=f"opening hands differ: only-old {sorted(set(ohand)-set(nhand))} "
                           f"vs only-new {sorted(set(nhand)-set(ohand))}")
    play_div = next((t for t in sorted(set(osig) | set(nsig))
                     if osig.get(t, []) != nsig.get(t, [])), None)
    draw_div = next((t for (t, c), (t2, c2) in zip(odraws, ndraws) if c != c2), None)
    if draw_div is None and len(odraws) != len(ndraws):
        draw_div = (odraws[len(ndraws)][0] if len(ndraws) < len(odraws) else ndraws[len(odraws)][0])
    if play_div is None:
        return dict(key=key, gi=gi, status="NO-PLAY-DIVERGENCE", old_win=old_win, new_win=new_win,
                    draw_div=draw_div)

    # Replay the shared prefix (turns < play_div), then rule on the old choice AT play_div.
    choices, verdict = [], None
    for _ in range(400):
        rc, out = play_step(new_bin, r, gi, force, choices)
        if rc == 0:
            break
        m = DEC_RE.search(out)
        if not m:
            verdict = dict(verdict="protocol-break"); break
        dec = json.loads(m.group(1))
        if dec.get("type") != "main_phase":
            choices.append(answer_aux(dec, None)); continue
        turn = dec.get("turn")
        want = oturns.get(turn) or []
        if turn < play_div:
            taken = None
            for cut in range(len(want), 0, -1):
                mv = VAL_RE.search(play_step(new_bin, r, gi, force, choices,
                                             validate=spec_of(want[:cut]))[1])
                if mv and json.loads(mv.group(1)).get("verdict") in ("accept", "choose"):
                    v = json.loads(mv.group(1))
                    idx = v.get("plan_index")
                    if idx is None or idx < 0:
                        vs = v.get("variants") or []
                        idx = vs[0]["plan_index"] if vs else None
                    if idx is not None and idx >= 0:
                        taken = (cut, idx); break
            if taken is None:
                choices.append(-1); oturns[turn] = []
            else:
                choices.append(taken[1]); oturns[turn] = want[taken[0]:]
            continue
        # This is the divergent turn. Ask the engine about the OLD choice, whole.
        mv = VAL_RE.search(play_step(new_bin, r, gi, force, choices, validate=spec_of(want))[1])
        v = json.loads(mv.group(1)) if mv else {}
        # A refusal of the WHOLE turn may just mean the engine needs two segments for it (a cantrip
        # mid-line draws the next card the line casts), so a prefix that is accepted still counts as
        # "the old opening move was available".
        pref = None
        if v.get("verdict") not in ("accept", "choose"):
            for cut in range(len(want) - 1, 0, -1):
                mv2 = VAL_RE.search(play_step(new_bin, r, gi, force, choices,
                                              validate=spec_of(want[:cut]))[1])
                if mv2 and json.loads(mv2.group(1)).get("verdict") in ("accept", "choose"):
                    pref = cut; break
        verdict = dict(verdict=v.get("verdict"), reason=v.get("reason"),
                       failed=v.get("failed_action"), spec=spec_of(want),
                       new_spec=spec_of(nturns.get(play_div) or []), prefix_ok=pref,
                       nwant=len(want))
        break
    return dict(key=key, gi=gi, status="OK", old_win=old_win, new_win=new_win,
                play_div=play_div, draw_div=draw_div, at=verdict)


# ---- can the old result be reached with more search? --------------------------------------------
LADDER = [(3, 200), (5, 500), (5, 5000), (6, 20000)]


def escalate_ab(mode, key, gi, new_bin):
    """Score the SAME game with the fixes on and off, at rising depth/budget.

    "Recovered with more search" on its own proves little: d0 -> d3 lifts both arms. The question is
    whether the two arms still DIFFER once the search is deep enough. If they converge, the batch
    costs nothing that search does not already recover, and the ground-truth move is an artifact of
    the tier's depth, not a quality loss."""
    cfg = explain.load_cases()
    r = explain.resolve(mode, key, cfg)
    rungs = []
    for depth, budget in [(r["depth"], r["budget"])] + LADDER:
        rr = dict(r, depth=depth, budget=budget)
        on, off = score_once(new_bin, rr, gi), score_once(new_bin, rr, gi, LEGACY_ENV)
        rungs.append((f"d{depth}/{budget}ms", off, on))
        if on is not None and off is not None and on <= off:
            return dict(key=key, gi=gi, rungs=rungs, converged=True)
    return dict(key=key, gi=gi, rungs=rungs, converged=False)


def escalate(mode, key, gi, new_bin, target):
    """Re-run ONE game with the fixes on, at rising depth/budget, until it reaches `target`.

    The point the case budget cannot settle: a removed ILLEGAL line is gone at any budget, but a
    line the search merely failed to find comes back when you give it more search. d0 cases are
    greedy by definition -- no search to give -- so escalating them asks whether the line is
    findable AT ALL, which is the same question one rung up."""
    cfg = explain.load_cases()
    r = explain.resolve(mode, key, cfg)
    out = []
    for depth, budget in LADDER:
        rr = dict(r, depth=depth, budget=budget)
        sc = score_once(new_bin, rr, gi)
        out.append((f"d{depth}/{budget}ms", sc))
        if sc is not None and sc <= target:
            return dict(key=key, gi=gi, target=target, rungs=out, recovered=True)
    return dict(key=key, gi=gi, target=target, rungs=out, recovered=False)


# ---- the changed-game lists, computed from the committed per-game wins ------------------------
LOSS = 9   # loss-penalized score for an unwon game (max_turns 8 + 1), as the audit and GT use


def read_wins(path):
    out = {}
    if not os.path.exists(path):
        return out
    for line in open(path):
        f = line.split()
        if len(f) >= 2:
            out[int(f[0])] = int(f[1])
    return out


def slower_games(mode, want_d0=False):
    """Every game whose loss-penalized score got WORSE, read straight off the committed per-game GT.

    Deliberately NOT parsed out of audit_changed_games.py's text: that report ENUMERATES the
    searched-depth slower games but only prints a few d0 EXAMPLES ("sanity-check a couple"), so
    scraping it silently covers a fraction of the d0 set -- and "look at every game" means every
    game.
    """
    cfg = explain.load_cases()
    games = []
    for spec in cfg["cases"].get(mode, []):
        f = spec.split()
        deck, depth, seed = f[0], int(f[1]), int(f[2])
        if depth == 0 and not want_d0:
            continue
        key = f"{deck}_{mode}_d{depth}_s{seed}"
        old = read_wins(os.path.join(HERE, "gt_logs", f"{key}.wins"))
        new = read_wins(os.path.join(HERE, "logs", mode, "wins", f"{key}.wins"))
        for gi, ow in sorted(old.items()):
            if gi not in new:
                continue
            os_, ns_ = (LOSS if ow < 0 else ow), (LOSS if new[gi] < 0 else new[gi])
            if ns_ > os_:
                games.append((key, gi, str(ow), str(new[gi])))
    return games


def main():
    argv = sys.argv[1:]
    if not argv:
        print(__doc__); return 2
    mode = argv[0]
    old_bin = new_bin = None
    jobs = 1
    for f, setter in (("--old-bin", "old"), ("--new-bin", "new"), ("--jobs", "jobs")):
        if f in argv:
            i = argv.index(f); val = argv[i + 1]; del argv[i:i + 2]
            if setter == "old":   old_bin = val
            elif setter == "new": new_bin = val
            else:                 jobs = int(val)
    want_d0 = "--d0" in argv
    if want_d0: argv.remove("--d0")
    global VERBOSE
    if "--verbose" in argv:
        VERBOSE = True; argv.remove("--verbose")
    old_bin = old_bin or os.path.join(ROOT, f"logs/snapshots/{mode}-baseline")
    new_bin = new_bin or os.path.join(ROOT, "build/Release/mtg")

    if "--escalate-ab" in argv:
        argv.remove("--escalate-ab")
        tg = ([(k, g) for k, g, _, _ in slower_games(mode, want_d0)] if "--slower" in argv
              else [(argv[1], int(g)) for g in argv[2:]])
        print(f"ESCALATION A/B (fixes OFF vs ON)  mode={mode}  games={len(tg)}")
        conv = 0
        with futures.ThreadPoolExecutor(max_workers=jobs) as ex:
            for a in ex.map(lambda t: escalate_ab(mode, t[0], t[1], new_bin), tg):
                conv += bool(a["converged"])
                rr = "  ".join(f"{n}:{off}->{on}" for n, off, on in a["rungs"])
                print(f"  {a['key']} gi{a['gi']}: "
                      f"{'CONVERGES' if a['converged'] else 'STILL WORSE at every rung'}  {rr}",
                      flush=True)
        print(f"\nthe two arms converge once searched: {conv}/{len(tg)}")
        return 0

    if "--escalate" in argv:
        argv.remove("--escalate")
        tg = ([(k, g, o) for k, g, o, _ in slower_games(mode, want_d0)] if "--slower" in argv
              else [(argv[1], int(g), None) for g in argv[2:]])
        print(f"ESCALATION  mode={mode}  games={len(tg)}  ladder={LADDER}")
        rec = 0
        with futures.ThreadPoolExecutor(max_workers=jobs) as ex:
            def run(t):
                tgt = 9.0 if t[2] in (None, "-1") else float(t[2])
                return escalate(mode, t[0], t[1], new_bin, tgt)
            for a in ex.map(run, tg):
                rec += bool(a["recovered"])
                rungs = "  ".join(f"{n}={v}" for n, v in a["rungs"])
                print(f"  {a['key']} gi{a['gi']} target<={a['target']}: "
                      f"{'RECOVERED' if a['recovered'] else 'NOT recovered'}  {rungs}", flush=True)
        print(f"\nrecovered with more search: {rec}/{len(tg)}")
        return 0

    if "--first-div" in argv:
        argv.remove("--first-div")
        tg = ([(k, g) for k, g, _, _ in slower_games(mode, want_d0)] if "--slower" in argv
              else [(argv[1], int(g)) for g in argv[2:]])
        print(f"FIRST DIVERGENT DECISION  mode={mode}  games={len(tg)}")
        rows = []
        with futures.ThreadPoolExecutor(max_workers=jobs) as ex:
            for a in ex.map(lambda t: first_divergence(mode, t[0], t[1], new_bin), tg):
                rows.append(a); print(fmt_div(a), flush=True)
        from collections import Counter
        c = Counter(str((r.get("at") or {}).get("verdict") or r["status"]) for r in rows)
        bad = [r for r in rows if r.get("play_div") and r.get("draw_div")
               and r["draw_div"] < r["play_div"]]
        print("\nold choice at the first divergent turn: " +
              ", ".join(f"{k}={v}" for k, v in sorted(c.items())))
        print(f"games where a DRAW diverged before any PLAY did: {len(bad)}"
              f"  (should be 0 -- the library is a fixed permutation of the seed)")
        return 0

    if "--attribute" in argv:
        argv.remove("--attribute")
        tg = ([(k, g) for k, g, _, _ in slower_games(mode, want_d0)] if "--slower" in argv
              else [(argv[1], int(g)) for g in argv[2:]])
        print(f"FIX ATTRIBUTION  mode={mode}  games={len(tg)}  (autonomous single-game scores)")
        agg = {}
        with futures.ThreadPoolExecutor(max_workers=jobs) as ex:
            for a in ex.map(lambda t: attribute(mode, t[0], t[1], new_bin), tg):
                tagn = ",".join(a["caused_by"]) or ("(none - not moved by this batch)"
                                                    if a["fixes_off"] == a["now"] else "(combination)")
                agg[tagn] = agg.get(tagn, 0) + 1
                print(f"  {a['key']} gi{a['gi']}: now={a['now']} fixes-off={a['fixes_off']}"
                      f"  <- {tagn}", flush=True)
        print("\nattribution: " + ", ".join(f"{k}={v}" for k, v in sorted(agg.items())))
        return 0

    if "--slower" in argv:
        targets = [(k, gi) for k, gi, _, _ in slower_games(mode, want_d0)]
    else:
        key = argv[1]
        targets = [(key, int(g)) for g in argv[2:]]
    if not targets:
        print("no games selected"); return 0

    print(f"GT-LINE PLAYABILITY  mode={mode}  games={len(targets)}  old={old_bin}")
    rows = []
    with futures.ThreadPoolExecutor(max_workers=jobs) as ex:
        for res in ex.map(lambda t: check_game(mode, t[0], t[1], old_bin, new_bin), targets):
            rows.append(res)
            print(fmt(res), flush=True)
    def cls(r):
        b, cb = r.get("blocked"), r.get("ctrl_blocked")
        if r["status"] != "WALKED":          return "error"
        if b and cb and cb.get("turn") == b.get("turn"): return "inconclusive"
        if b and b.get("verdict") == "unsupported":      return "unsupported"
        if b:                                            return "NOW-ILLEGAL"
        if r.get("unplayed"):                            return "partial"
        return "legal"
    from collections import Counter
    c = Counter(cls(r) for r in rows)
    print(f"\nsummary over {len(rows)} game(s): " +
          ", ".join(f"{k}={v}" for k, v in sorted(c.items())))
    print("  NOW-ILLEGAL  = the current engine refuses the old line where the fixes-OFF control plays it")
    print("  legal        = the old line replays in full under the current engine")
    print("  inconclusive = the fixes-OFF control refuses the same turn (harness drift, not a finding)")
    return 0


def fmt_div(a):
    head = f"  {a['key']} gi{a['gi']}: "
    if a["status"] != "OK":
        return head + a["status"] + (f"  (draws diverge T{a.get('draw_div')})" if a.get("draw_div") else "")
    v = a.get("at") or {}
    t = lambda x: ("loss" if x is None else f"T{x}")
    where = (f"play diverges T{a['play_div']}, draws T{a.get('draw_div') or '-'}  "
             f"[{t(a['old_win'])} -> {t(a['new_win'])}]")
    if v.get("verdict") in ("accept", "choose"):
        return head + f"{where}\n      old choice STILL AVAILABLE ({v['verdict']}): {v['spec']}\n" \
                      f"      engine took instead:              {v['new_spec'] or '(nothing)'}"
    pf = (f", but its first {v['prefix_ok']}/{v['nwant']} actions are"
          if v.get("prefix_ok") else ", and no prefix of it is")
    return head + f"{where}\n      old choice REFUSED ({v.get('verdict')}){pf} available\n" \
                  f"      line: {v.get('spec')}\n      reason: {v.get('reason') or '-'}"


def fmt(r):
    head = f"  {r['key']} gi{r['gi']}: "
    dv = r.get("diverge")
    tail_dv = f"  [draws diverge from T{dv}]" if dv else "  [same draws]"
    if r["status"] != "WALKED":
        return head + r["status"] + " " + str(r.get("detail", ""))
    b, cb = r.get("blocked"), r.get("ctrl_blocked")
    if b:
        # A refusal the CONTROL reproduces is the harness losing the line, not the engine refusing
        # it (see check_game). Say which it is; only the first kind is evidence.
        if cb and cb.get("turn") == b.get("turn"):
            return (head + f"INCONCLUSIVE  T{b['turn']}: the fixes-OFF control refuses the same "
                    f"turn ({cb.get('verdict')}) -- the forced walk drifted, not a finding")
        if b.get("verdict") == "unsupported":
            return (head + f"UNSUPPORTED  T{b['turn']}: {b.get('reason')}")
        return (head + f"OLD LINE NOW REFUSED  T{b['turn']}/{b['phase']}  verdict={b['verdict']}"
                f"  (plays with the fixes OFF)\n"
                f"      line: {b['spec']}\n"
                f"      failed: {b.get('failed_action') or '-'}  reason: {b.get('reason') or '-'}")
    if r.get("unplayed"):
        return head + f"partially replayed; not reached: {r['unplayed']}"
    ow, rw, cw = r.get("old_win"), r.get("replay_win"), r.get("old_self")
    t = lambda x: ("loss" if x is None else f"T{x}")
    if rw == ow:
        return (head + f"old line is LEGAL and still reaches {t(ow)} when forced -- the engine had it"
                + tail_dv)
    if cw != ow:
        return (head + f"old line is LEGAL; forced replay {t(rw)} vs recorded {t(ow)}, but the "
                f"fixes-OFF control also lands {t(cw)} -- outcome INCONCLUSIVE" + tail_dv)
    return (head + f"old line is LEGAL and the control reproduces {t(ow)}, but the current binary "
            f"forced down it lands {t(rw)} -- a real outcome change" + tail_dv)


if __name__ == "__main__":
    sys.exit(main())

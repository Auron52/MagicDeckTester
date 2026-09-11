#!/usr/bin/env python3
"""COMBO OFF REPLAY HUNT -- the same question as test/combo_off_sweep.py, at TRUE fidelity.

The phase-1 sweep (docs/design/combo-off-sweep-catalogue.md) asks every state through a
`--scenario` fixture, and §6 of that catalogue lists what a fixture cannot stage:

    * NO FLOATING MANA        -- a reconstructed frame is asked with an empty pool,
    * LIBRARY ORDER IS SYNTHETIC (canonical decklist order, not the real shuffle),
    * EXILE IS INVISIBLE      -- a resolved Living Wish exiles itself; the rebuild over-counts,
    * ENERGY IS NOT RECONSTRUCTED (an Aether Hub reads as {C}-only).

Every one of those is a property of the RECONSTRUCTION, not of the engine.  The stateless
`--claude-play` replay protocol has none of them: it replays the real deterministic game, so the
pool, the shuffle, the exile zone and the energy counters are whatever they really were.  This
harness therefore re-asks the same question on states the sweep cannot reach -- and in particular
on the MID-TURN boards the user's own failures happened on (seed 6 T4: "Combo Off drew 13 cards and
stopped, leaving no mana up"; seed 9 T4), which are re-prompt frames reached with mana floating.

WHAT IT DOES

  1. REFERENCE POPULATION.  Each references/EldraziDisplacerFlicker/claude_s*_gi*.json is replayed
     BY INTENT using test/viewer_protocol_check.py's own walk (check_reference(collect=...) hands
     back the content-resolved pick stream plus, per aligned main-phase frame, the prefix length
     that lands the engine on it).  At every such frame the decision dump is read; whenever it
     carries a plan with `combo_off: true` the harness BRANCHES -- appends that plan's index and
     runs to terminal -- and records whether the click won THAT turn.

  2. AUTONOMOUS-PROXY POPULATION.  Seeds 13..72 at --game-index seed-1 (the user's own
     claude_s<N>_gi<N-1> convention), driven forward one decision at a time and branched the same
     way.  SEE THE PROTOCOL NOTE BELOW: the protocol exposes no engine pick for a main_phase
     frame, so the driving policy is this harness's own (documented) develop-greedy, not the
     engine's.

  3. FLOAT-STAGED VARIANTS.  Every probed state is also asked with a real floating pool staged by
     hand, through the viewer's own `tap=<Card>#<num>:<COLOUR>` tokens riding `--cast-order`
     (docs/design/viewer-manual-tap-pay.md).  Two arms: every untapped source tapped for its first
     legal face, and only the {C}-capable sources tapped for {C}.  This is the one thing the
     scenario sweep provably cannot do.

  4. CLASSIFICATION.  Verbatim `combo_off_sweep.classify()` over a record built the same shape, so
     the a/b/c/d/e counts are directly comparable with the phase-1 catalogue; the python
     arithmetic oracle (`combo_off_sweep.arith`) and the cluster tagger
     (`combo_off_sweep.mechanism`) are likewise imported, not re-derived.

  5. REPROS.  Every class-b and class-c state carries an exact, pasteable one-line command, and
     every one is RE-RUN and verified to reproduce before it is recorded.

PROTOCOL NOTE (verified in src/main.cpp, not assumed).  `WriteDecisionJson`'s main_phase frame
emits `main_ordinal`, `type`, `turn`, `phase`, `on_the_play`, the board, and `plans` -- and NO
`heuristic_default` / `ai_choice`.  Those keys exist only on the auxiliary frames (mulligan,
bottom, vial_charge, free_cast, target, ...).  `-1` in --choices means PASS (cast nothing), not
"let the engine choose", and there is no --auto flag.  So "answer every decision with the engine's
own default pick" is NOT EXPRESSIBLE for a main-phase decision.  The auxiliary frames ARE answered
from their own default (viewer_protocol_check.engine_default); main-phase frames are answered by
the policy in `drive_pick`.  This gap is reported as a protocol anomaly rather than papered over.

Usage:
    bash test/combo_off_replay_hunt.sh --quick      # references + 12 seeds, minutes
    bash test/combo_off_replay_hunt.sh              # references + 60 seeds
    python3 test/combo_off_replay_hunt.py --help
"""
import argparse
import concurrent.futures as futures
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "test"))

import viewer_protocol_check as vpc          # noqa: E402  -- replay/anchoring, reused verbatim
import combo_off_sweep as sw                 # noqa: E402  -- arith / mechanism / classify, verbatim

MTG = sw.MTG
DECK = sw.DECK
PROF = "decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json"
REFDIR = sw.REFDIR
OUTDIR = os.path.join(ROOT, "logs/combo_off_hunt")

# The viewer's own display cap is 200; the reference anchoring walk turns it OFF so a recorded pick
# beyond the cap is still addressable (see viewer_protocol_check.replay).  Kept off here too, so a
# combo-off plan can never be missed because it sat past a truncation.
REPLAY_ENV = {"MTG_PLAY_PLANS_CAP": "0"}

# ...except on the DRIVEN walks, which use the viewer's own default cap.  Two reasons, and
# neither is a fidelity compromise: (a) the cap changes only the emitted SLICE, never the
# enumeration or the engine's behaviour, and plan `index` stays the true engine index, so every
# pick and every repro is unaffected; (b) the verified/offered COMBO OFF plan has a RESERVED slot
# that the cap cannot evict (src/main.cpp, "THE VERIFIED COMBO OFF PLAN IS NEVER CAPPED OUT"), so
# no offer can be missed.  What it buys is not paying to serialise and parse 11,453 plans per
# frame on a mid-go-off board.  The reference walks stay UNCAPPED, matching
# viewer_protocol_check's own replay so the two harnesses see literally the same menus.
DRIVEN_ENV = {"MTG_PLAY_PLANS_CAP": "200"}

# A go-off turn re-prompts once per committed segment; the user's own s1_gi0 T3 runs to 45 frames.
# Cap the driven line well above that so a pathological loop cannot hang the run, and record the
# cap being hit rather than silently truncating.
MAX_DRIVEN_DECISIONS = 400


# ---------------------------------------------------------------------------------------------
# One engine invocation.
# ---------------------------------------------------------------------------------------------
def run(seed, gi, choices, force=None, extra=None, max_turns=8, env_extra=None):
    """One stateless --claude-play replay.  Returns (rc, stdout+stderr, argv)."""
    args = [MTG, DECK, "--claude-play", "--seed", str(seed), "--game-index", str(gi),
            "--max-turns", str(max_turns), "--depth", "0", "--profile", PROF,
            "--choices", ",".join(str(c) for c in choices)]
    if force is not None:
        args += ["--force-mulligan", force]
    if extra:
        args += list(extra)
    env = dict(os.environ)
    env.update(REPLAY_ENV)
    env.update(env_extra or {})
    p = subprocess.run(vpc.capped(args), capture_output=True, text=True, cwd=ROOT, env=env)
    return p.returncode, p.stdout + p.stderr, args


class Session:
    """One PERSISTENT engine child (`--claude-play --interactive`), fed picks on stdin.

    The stateless form re-executes the whole prefix on every step, so walking an N-decision game
    costs O(N^2) game-executions -- and an EDF go-off turn is 45-100 decisions, which is why the
    first cut of this harness took ~25 min on eleven references.  `--interactive`
    (ClaudePlayHarness::AwaitMoreChoices) keeps the child alive: it flushes the decision frame,
    blocks on a line of stdin, and resumes with the new picks appended.  Same engine, same
    determinism, same frames -- one game-execution instead of N.

    The REPROS this harness prints are still the stateless `--choices` one-liners, because that
    is what a fixer can paste; the session is only how the hunt gets there.
    """

    def __init__(self, seed, gi, force=None, extra=None, max_turns=8, env_extra=None):
        self.argv = [MTG, DECK, "--claude-play", "--interactive",
                     "--seed", str(seed), "--game-index", str(gi),
                     "--max-turns", str(max_turns), "--depth", "0", "--profile", PROF,
                     "--choices", ""]
        if force is not None:
            self.argv += ["--force-mulligan", force]
        if extra:
            self.argv += list(extra)
        env = dict(os.environ)
        env.update(REPLAY_ENV)
        env.update(env_extra or {})
        # stderr to a TEMP FILE, never a pipe: the diagnostic arms (MTG_EDF_GOFF_DEBUG /
        # MTG_EDF_FINISH_STATS) write while the child is blocked on stdin, and a full 64K pipe
        # buffer with nobody draining it is a deadlock.  A file cannot block.
        self._err = tempfile.TemporaryFile(mode="w+")
        self.proc = subprocess.Popen(vpc.capped(self.argv), stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=self._err,
                                     text=True, cwd=ROOT, env=env, bufsize=1)
        self.picks = []
        self.err_text = ""

    def read(self):
        """Next frame: ('decision', obj) | ('result', obj) | ('eof', tail-text)."""
        buf = []
        while True:
            line = self.proc.stdout.readline()
            if not line:
                return "eof", "".join(buf[-12:])
            buf.append(line)
            if line.startswith("<<<END_DECISION>>>"):
                d = decision_of("".join(buf))
                return ("decision", d) if isinstance(d, dict) else ("eof", "malformed decision")
            if line.startswith("<<<END_RESULT>>>"):
                r = result_of("".join(buf))
                return ("result", r) if isinstance(r, dict) else ("eof", "malformed result")

    def send(self, pick):
        self.picks.append(int(pick))
        try:
            self.proc.stdin.write("%d\n" % int(pick))
            self.proc.stdin.flush()
        except (BrokenPipeError, ValueError):
            pass

    def close(self):
        try:
            if self.proc.stdin and not self.proc.stdin.closed:
                self.proc.stdin.close()
        except Exception:                                        # noqa: BLE001
            pass
        try:
            self.proc.wait(timeout=20)
        except Exception:                                        # noqa: BLE001
            self.proc.kill()
        try:
            self._err.seek(0)
            self.err_text = self._err.read() or ""
            self._err.close()
        except Exception:                                        # noqa: BLE001
            pass
        try:
            self.proc.stdout.close()
        except Exception:                                        # noqa: BLE001
            pass


def stateless_argv(seed, gi, choices, force=None, extra=None, max_turns=8):
    """The argv of the PASTEABLE stateless equivalent of a session prefix."""
    args = [MTG, DECK, "--claude-play", "--seed", str(seed), "--game-index", str(gi),
            "--max-turns", str(max_turns), "--depth", "0", "--profile", PROF,
            "--choices", ",".join(str(c) for c in choices)]
    if force is not None:
        args += ["--force-mulligan", force]
    if extra:
        args += list(extra)
    return args


def repro_cmd(argv, env_extra=None):
    """The pasteable one-liner for `argv` (relative binary, quoted args, env prefix)."""
    def q(a):
        return a if re.fullmatch(r"[A-Za-z0-9_./=:+-]*", a) else "'" + a.replace("'", "'\\''") + "'"
    env = dict(REPLAY_ENV)
    env.update(env_extra or {})
    pre = " ".join("%s=%s" % kv for kv in sorted(env.items()))
    body = " ".join(q(a) for a in [os.path.relpath(argv[0], ROOT)] + argv[1:])
    return (pre + " " + body).strip()


def decision_of(out):
    m = vpc.DEC_RE.search(out)
    if not m:
        return None
    try:
        return json.loads(m.group(1))
    except json.JSONDecodeError:
        return "malformed"


def result_of(out):
    m = vpc.RES_RE.search(out)
    if not m:
        return None
    try:
        return json.loads(m.group(1))
    except json.JSONDecodeError:
        return "malformed"


# ---------------------------------------------------------------------------------------------
# Reading a frame.
# ---------------------------------------------------------------------------------------------
def combo_plans(dec):
    if "n_plans" in dec:                     # already compacted (see compact_frame)
        return dec.get("plans") or []
    return [p for p in (dec.get("plans") or []) if p.get("combo_off")]


def compact_frame(dec):
    """Drop the plan list down to what a probe actually reads, before the frame is RETAINED.

    A walk keeps every main-phase frame it passed through so the probes can run after the
    terminal is known -- and an EDF mid-go-off board enumerates 1,400-208,392 plans, so holding
    the full JSON for 400 frames x 14 concurrent walks is tens of gigabytes.  It was measured the
    hard way: the first full run was OOM-killed at 35 GB RSS, 50 jobs in.  Nothing downstream
    needs a non-combo plan -- `probe_frame` reads the board, the combo-off plan(s) and the plan
    COUNT -- so keep exactly those and let the rest go.
    """
    keep = dict(dec)
    plans = dec.get("plans") or []
    keep["n_plans"] = len(plans)
    keep["plans"] = [p for p in plans if p.get("combo_off")]
    return keep


def board_summary(dec):
    """Compact, human-readable board -- enough for a fixer to recognise the position without
    re-running anything, and enough for the state id to be content-addressed."""
    me = dec.get("me") or {}
    bf = me.get("battlefield") or []
    by_num = {c.get("num"): c.get("name") for c in bf}
    auras = {}
    for c in bf:
        h = c.get("attached_to")
        if h is not None and h in by_num:
            auras.setdefault(h, []).append(c["name"])
    perms = []
    for c in bf:
        if c.get("attached_to") in by_num:
            continue
        tag = c["name"]
        if auras.get(c.get("num")):
            tag += "+" + "+".join(sorted(auras[c["num"]]))
        if c.get("tapped"):
            tag += " (T)"
        perms.append(tag)
    return {
        "battlefield": sorted(perms),
        "untapped_sources": sorted(c["name"] for c in bf
                                   if c.get("taps") and not c.get("tapped")),
        "hand": sorted(c.get("name", "") for c in (me.get("hand") or [])),
        "graveyard": sorted(c.get("name", "") for c in (me.get("graveyard") or [])
                            if isinstance(c, dict)),
        "library_size": me.get("library_size"),
        "floating_mana": me.get("floating_mana"),
        "energy": me.get("energy"),
        "my_life": me.get("life"),
        "opp_life": (dec.get("opponent") or {}).get("life"),
        "land_drops_left": me.get("land_drops_left"),
    }


def state_id(population, seed, gi, dec):
    b = board_summary(dec)
    sig = json.dumps({"pop": population, "seed": seed, "gi": gi,
                      "turn": dec.get("turn"), "phase": dec.get("phase"),
                      "ord": dec.get("main_ordinal"), "b": b}, sort_keys=True)
    return "%s-%s" % ({"reference": "RR", "autonomous": "AA"}.get(population, "ZZ"),
                      hashlib.sha1(sig.encode()).hexdigest()[:10])


def tap_tokens(dec, only_c=False):
    """`tap=<Card>#<num>:<COLOUR>` for every hand-tappable untapped source on this board.

    The faces come from the engine's OWN legality answer (`taps`, = HumanPreTapFaces), so the
    harness can never ask for a tap ApplyHumanPreTap would refuse -- one function, no mirrored
    rule (docs/design/viewer-manual-tap-pay.md).  `only_c` keeps just the {C}-capable sources and
    taps them for {C}, which is the arm that matters for a Displacer loop: the {2}{C} blink and
    the Depleter's {1}{C} drain need PIPS, and no amount of coloured mana pays a {C} (CR 107.4c).
    """
    out = []
    for c in (dec.get("me") or {}).get("battlefield") or []:
        faces = c.get("taps") or ""
        if not faces or c.get("tapped"):
            continue
        if only_c:
            if "C" not in faces:
                continue
            face = "C"
        else:
            face = faces[0]
        out.append("tap=%s#%s:%s" % (c["name"], c.get("num"), face))
    return out


# ---------------------------------------------------------------------------------------------
# The click, and what it did.
# ---------------------------------------------------------------------------------------------
EVENT_KEYS = (
    ("blink", re.compile(r"blink", re.I)),
    ("drain", re.compile(r"loses \d+ life|drain", re.I)),
    ("exile", re.compile(r"exile", re.I)),
    ("draw", re.compile(r"\bdrew\b|draws? a card|draw \d", re.I)),
    ("wish", re.compile(r"Living Wish", re.I)),
    ("manual_tap", re.compile(r"manual tap", re.I)),
    ("gorge", re.compile(r"Shivan Gorge", re.I)),
)


def summarise_events(events):
    """What the executor ACTUALLY did, counted off the play-event stream the viewer renders."""
    ev = {k: 0 for k, _ in EVENT_KEYS}
    ev["total"] = len(events or [])
    casts, texts = [], []
    for e in (events or []):
        t = e.get("text") or ""
        texts.append(t)
        for k, rx in EVENT_KEYS:
            if rx.search(t):
                ev[k] += 1
        m = re.match(r"^(?:⟶\s*)?cast(?:s)?:?\s*(.+?)(?:\s+--|\s*$)", t, re.I)
        if m:
            casts.append(m.group(1))
    ev["casts"] = casts
    ev["failed"] = [t for t in texts
                    if "COMBO OFF did not finish" in t or "COMBO OFF did NOT win" in t]
    ev["texts"] = texts[-40:]          # bounded: the tail is what explains a failure
    return ev


def click(seed, gi, force, side, prefix, plan_index, max_turns, taps=None, main_ordinal=None,
          env_extra=None, want_raw=False):
    """Click the combo-off plan at this frame and RUN THE LINE OUT TO A TERMINAL.

    `taps` (with `main_ordinal`) rides --cast-order as a TAPS-ONLY list, which
    AIEngine::ReorderPlanCasts lifts out and ApplyPlanDirect flushes UP FRONT -- i.e. the go-off
    is applied on a board that already has a real floating pool.  A taps-only list deliberately
    does NOT set `searched_order`, so the plan's own action order is untouched: the only
    difference between that arm and the plain one is the mana supply.

    RUNNING OUT TO A TERMINAL IS NOT A DETAIL -- getting it wrong inverts the headline number.
    A COMBO OFF that wins by DECKING (Dimensional Infiltrator's exile sink) leaves the opponent
    at 20 life with an empty library, which is NOT a loss at that instant: CR 104.3c makes it a
    loss the next time they would draw, and the engine correctly emits one more
    "cast: (nothing)" frame first.  Reading "a further decision frame appeared" as "the click
    did not win" reported every deck-out kill as an executor failure (measured: 4/4 on
    claude_s2_gi1 before this was fixed, 0/4 after).  So every remaining main-phase frame is
    PASSED -- doing nothing further is what "the go-off is the whole line" means -- and every
    auxiliary frame is answered from its own engine default.
    """
    extra = list(side or [])
    if taps and main_ordinal is not None and main_ordinal >= 0:
        extra += ["--cast-order", "%d:%s" % (main_ordinal, "|".join(taps))]
    info = {"env": dict(env_extra or {}),
            "argv": stateless_argv(seed, gi, list(prefix) + [plan_index], force, extra, max_turns)}
    s = Session(seed, gi, force, extra, max_turns, env_extra)
    first_events, tail_events, first_dec = None, [], None
    try:
        for p in prefix:
            kind, obj = s.read()
            if kind != "decision":
                info.update(error="prefix replay ended early (%s)" % kind, won=None,
                            events=summarise_events([]))
                return info
            s.send(p)
        kind, obj = s.read()
        if kind != "decision":
            info.update(error="the probed frame did not re-appear (%s)" % kind, won=None,
                        events=summarise_events([]))
            return info
        s.send(plan_index)
        for _ in range(24):
            kind, obj = s.read()
            if kind == "eof":
                info.update(error="engine ended without a terminal: %s" % str(obj)[-200:],
                            won=None)
                break
            if kind == "result":
                evs = obj.get("events") or []
                if first_events is None:
                    first_events = summarise_events(evs)
                else:
                    tail_events += evs
                info.update(won=bool(obj.get("won")), win_turn=obj.get("win_turn"),
                            opp_life=(obj.get("opponent") or {}).get("life"))
                break
            evs = obj.get("events") or []
            if first_events is None:
                first_events, first_dec = summarise_events(evs), obj
                info["float_after"] = (obj.get("me") or {}).get("floating_mana")
            else:
                tail_events += evs
            s.send(-1 if obj.get("type") == "main_phase" else vpc.engine_default(obj)[0])
        else:
            info.update(error="the line after the click did not terminate within 24 frames",
                        won=None)
    finally:
        s.close()
    info["events"] = first_events or summarise_events([])
    if tail_events:
        info["events"]["failed"] += summarise_events(tail_events)["failed"]
        info["tail_frames"] = True
    info["picks_to_terminal"] = len(s.picks) - len(prefix)
    info["terminal_argv"] = stateless_argv(seed, gi, s.picks, force, extra, max_turns)
    if want_raw:
        info["_raw"] = s.err_text
        info["_events"] = ((first_dec or {}).get("events") or [])
    return info


# ---------------------------------------------------------------------------------------------
# Classification -- combo_off_sweep's own functions, over a record of the same shape.
# ---------------------------------------------------------------------------------------------
def sweep_record(dec, offered, verified, rule, apply_win, opp_life_after, opp_lib_after,
                 line_win_turn, error=None):
    """Build the `rec` dict combo_off_sweep.classify()/mechanism() expect, from a LIVE frame.

    The fixture is reconstructed with the sweep's own `fixture_from_snapshot`, purely so the
    python arithmetic oracle (`arith`) and the combat-lethal split see exactly what they see in
    the sweep.  Nothing in this harness ASKS the engine through that fixture -- the engine was
    asked through the replay -- so the fixture's documented reconstruction limits (canonical
    library order, invisible exile) bound only the class-(d) oracle, never the a/b/c verdicts.
    """
    fix = sw.fixture_from_snapshot(dec)
    if fix is None:
        return None
    hosts, bf = {}, []
    for p in fix["battlefield"]:
        if p.get("equips"):
            hosts.setdefault(p["equips"], []).append(p["name"])
    for p in fix["battlefield"]:
        if p.get("equips"):
            continue
        bf.append((p["name"], p.get("tapped", False), hosts.get(p["name"], [])))
    _main, side = sw.deck_main_and_side()
    library = fix.get("library_top", [])
    a = sw.arith(bf, fix["hand"], library, list(side), fix["opponent_life"],
                 fix["opponent_library_size"], fix.get("energy_counters", 0))

    def is_fin(n):
        return bool((sw.par(n, "drain_cost") and (sw.par(n, "drain_amount", 0) or 0) > 0)
                    or sw.par(n, "exile_opponent_top_cost"))

    power = 0
    for name, _t, _au in bf:
        c = sw.cards().get(name) or {}
        try:
            power += int(c.get("power") or 0)
        except (TypeError, ValueError):
            pass
    turn = dec.get("turn")
    return {
        "turn": turn,
        "offer": {"offered": offered, "verified": verified, "rule": rule,
                  "apply_win": apply_win, "opp_life_after": opp_life_after,
                  "opp_lib_after": opp_lib_after, "error": error,
                  "plans": dec.get("n_plans", len(dec.get("plans") or [])), "summary": None},
        "arith": a,
        "untap_confound": any(p.get("tapped") for p in fix["battlefield"]),
        "wish_in_hand": any(sw.par(h, "wish_from_sideboard") for h in fix["hand"]),
        "wish_in_lib_only": (not any(sw.par(h, "wish_from_sideboard") for h in fix["hand"])
                             and any(sw.par(l, "wish_from_sideboard") for l in library)),
        "finisher_reachable": (any(is_fin(n) for n, _, _ in bf)
                               or any(is_fin(h) for h in fix["hand"])
                               or any(sw.par(h, "wish_from_sideboard") for h in fix["hand"])),
        "board_power": power,
        "combat_lethal": power >= fix["opponent_life"],
        # The winnability oracle here is NOT a second search: it is what the replayed line ITSELF
        # did.  A frame at turn T counts as winnable-this-turn iff the continuation actually won
        # on turn T.  That is a strict LOWER bound (the line played is one line, not the best
        # one), where the sweep's probe_win is an UPPER bound -- the two bracket the truth from
        # opposite sides, which is worth saying out loud when the two harnesses are compared.
        "win": {"auto_win_turn": turn if (line_win_turn == turn) else None},
        "fixture": fix,
    }


# Cluster names from the phase-1 catalogue, keyed off combo_off_sweep.mechanism()'s tags.
# Order matters: the first matching row wins, most specific first.
CLUSTER_RULES = [
    ("C2", lambda t: "damage-sink-starves-loop" in t),
    ("C1b", lambda t: "draw-to-find-starves-loop" in t or
     ("loop-zero-iterations" in t and "wish-in-library-dig" in t)),
    ("C1a", lambda t: "draw-loop-never-ran" in t or "draw-loop-stopped-early" in t),
    ("C6", lambda t: "loop-ran-finish-never-fired" in t),
    ("C3", lambda t: ("c-net-nonpositive" in t or "c-net-negative" in t)
     and "finish-fired-short" in t),
    ("C5", lambda t: "finish-fired-short" in t or "decked-out-but-alive" in t),
    ("C3", lambda t: "c-net-nonpositive" in t or "c-net-negative" in t),
]


def cluster_of(tags):
    for name, pred in CLUSTER_RULES:
        if pred(tags):
            return name
    # Not one of the six catalogued shapes -- name the NEW cluster after its dominant tag so a
    # fixer can see at a glance what is new rather than being told "other".
    lead = next((t for t in tags if t != "other"), None)
    return "N:" + lead if lead else "N:unexplained"


# ---------------------------------------------------------------------------------------------
# Probing ONE frame.
# ---------------------------------------------------------------------------------------------
def probe_frame(ctx, prefix, dec, line_win_turn, frame_argv):
    """Everything this harness has to say about one main-phase decision frame."""
    seed, gi, force, side, mt, pop = (ctx["seed"], ctx["gi"], ctx["force"], ctx["side"],
                                      ctx["mt"], ctx["pop"])
    sid = state_id(pop, seed, gi, dec)
    turn = dec.get("turn")
    ordn = dec.get("main_ordinal")
    cos = combo_plans(dec)
    rec = {
        "id": sid, "population": pop, "seed": seed, "gi": gi,
        "turn": turn, "phase": dec.get("phase"), "main_ordinal": ordn,
        "decision_index": dec.get("decision_index"),
        "prefix_len": len(prefix),
        "n_plans": dec.get("n_plans", len(dec.get("plans") or [])),
        "board": board_summary(dec),
        "line_win_turn": line_win_turn,
        "offered": bool(cos),
        "anomalies": [],
    }
    # The frame itself, reproducible on its own: for a MISSED OFFER (class c) there is no click to
    # run, so the repro a fixer needs is "put the engine back on this board and look at the menu".
    rec["frame_repro"] = repro_cmd(frame_argv)
    if cos:
        p = cos[0]
        rec.update(plan_index=p["index"],
                   verified=bool(p.get("combo_off_verified")),
                   rule=p.get("combo_off_rule"),
                   plan_summary=p.get("summary"))
        if len(cos) > 1:
            rec["n_combo_plans"] = len(cos)
        if not p.get("combo_off_rule"):
            # The catalogue saw this once (R_claude_s11_gi10_58) and called it minor.  It is
            # recorded here per state, because the viewer's rule badge renders blank and a saved
            # frame then cannot say WHICH rule fired.
            rec["anomalies"].append("combo_off plan carries an EMPTY combo_off_rule")
        # ---- the click, plain -------------------------------------------------------------
        plain = click(seed, gi, force, side, prefix, p["index"], mt)
        # "WON" for a COMBO OFF means won THIS TURN.  A click at turn 4 whose line happens to
        # win on turn 6 is not the button doing what it says; the button's own wording is
        # "wins this turn".
        plain["won_this_turn"] = bool(plain.get("won")) and plain.get("win_turn") == turn
        rec["click"] = {k: v for k, v in plain.items()
                        if k not in ("argv", "terminal_argv", "_raw", "_events")}
        rec["repro"] = repro_cmd(plain["argv"])
        if plain.get("terminal_argv"):
            rec["repro_terminal"] = repro_cmd(plain["terminal_argv"])
        if plain.get("error"):
            rec["anomalies"].append("click: " + plain["error"])
        if plain.get("won") and plain.get("win_turn") != turn:
            rec["anomalies"].append(
                "combo_off click won on turn %s, not the turn it was offered on (%s)"
                % (plain.get("win_turn"), turn))
        # ---- the two FLOAT-STAGED arms ----------------------------------------------------
        # This is the fidelity the scenario sweep cannot reach: a REAL pool, staged through the
        # viewer's own manual-tap tokens, present when the go-off's payments run.
        for arm, only_c in (("float_all", False), ("float_c", True)):
            toks = tap_tokens(dec, only_c=only_c)
            if not toks or ordn is None or ordn < 0:
                rec.setdefault("float", {})[arm] = {"staged": False,
                                                    "why": "no hand-tappable source"
                                                    if not toks else "frame carries no main_ordinal"}
                continue
            f = click(seed, gi, force, side, prefix, p["index"], mt, taps=toks, main_ordinal=ordn)
            ev = f.get("events") or {}
            won_tt = bool(f.get("won")) and f.get("win_turn") == turn
            rec.setdefault("float", {})[arm] = {
                "staged": True, "taps": toks,
                "taps_applied": ev.get("manual_tap", 0),
                "won": f.get("won"), "win_turn": f.get("win_turn"), "won_this_turn": won_tt,
                "opp_life": f.get("opp_life"),
                "blinks": ev.get("blink"), "drains": ev.get("drain"), "exiles": ev.get("exile"),
                "changed_win": (won_tt != plain.get("won_this_turn")),
                "repro": repro_cmd(f["argv"]),
                "error": f.get("error"),
            }
        # ---- mechanism, for a click that did not win --------------------------------------
        if plain.get("won_this_turn") is False:
            tr = click(seed, gi, force, side, prefix, p["index"], mt, want_raw=True,
                       env_extra={"MTG_EDF_GOFF_DEBUG": "1", "MTG_EDF_FINISH_STATS": "1"})
            rec["trace"] = parse_trace(tr, p.get("summary"))
            rec["trace_repro"] = repro_cmd(tr["argv"], tr["env"])
    # ---- classification ---------------------------------------------------------------------
    apply_win = rec.get("click", {}).get("won_this_turn") if cos else None
    srec = sweep_record(dec, bool(cos), rec.get("verified", False), rec.get("rule"),
                        apply_win, rec.get("click", {}).get("opp_life"), None,
                        line_win_turn, error=rec.get("click", {}).get("error"))
    if srec is None:
        rec["class"] = "x_error"
        rec["anomalies"].append("could not reconstruct a fixture for the arithmetic oracle")
        return rec
    srec["trace"] = _sweep_trace(rec.get("trace"))
    rec["class"] = sw.classify(srec)
    rec["untap_confound"] = srec["untap_confound"]
    rec["wish_in_lib_only"] = srec["wish_in_lib_only"]
    rec["finisher_reachable"] = srec["finisher_reachable"]
    rec["combat_lethal"] = srec["combat_lethal"]
    rec["loop_net_c"] = srec["arith"]["loop"].get("net_c")
    rec["loop_ok"] = srec["arith"]["loop"].get("ok")
    rec["arith_unwinnable"] = srec["arith"]["unwinnable"]
    if rec["class"] in ("b_executor_failure", "d_rule_too_loose"):
        rec["mechanism"] = sw.mechanism(srec)
        rec["cluster"] = cluster_of(rec["mechanism"])
    return rec


def parse_trace(tr, plan_summary):
    """The engine's own instruments off a traced click, in combo_off_sweep.probe_trace's SHAPE.

    Same two regexes the sweep uses (`[edf-goff]` recognizer arithmetic, `[finish]` counters), so
    a tag produced downstream by combo_off_sweep.mechanism() means exactly what it means in the
    phase-1 catalogue.  The event tally is read from the protocol's own play-event stream (the
    list the viewer's history renders) rather than the scenario harness's `scenario: hist [...]`
    lines -- same events, different transport.
    """
    out = tr.get("_raw", "")
    t = {}
    g = sw.GOFF_RE.search(out)
    if g:
        t["goff"] = dict(outlet=g.group(2), payload=g.group(3), ok=g.group(4) == "1",
                         net=int(g.group(5)), refund=int(g.group(6)), cost=int(g.group(7)),
                         gorge_dmg=int(g.group(8)), gorge_cost=int(g.group(9)),
                         drain_amount=int(g.group(10)), drain_cost=int(g.group(11)),
                         exile_cost=int(g.group(12)), kmax=int(g.group(13)),
                         afford=int(g.group(14)), iterations=int(g.group(15)))
    fs = sw.FINSTAT_RE.search(out)
    if fs:
        t["finish"] = dict(draw_seen=int(fs.group(1)), draw_guard=int(fs.group(2)),
                           draw_paid=int(fs.group(3)), calls=int(fs.group(4)),
                           hand=int(fs.group(5)), wish=int(fs.group(6)),
                           no_mana=int(fs.group(7)), none_found=int(fs.group(8)),
                           pay_fail=int(fs.group(9)))
    tally, texts = {}, []
    for e in tr.get("_events") or []:
        text, kind = e.get("text") or "", e.get("kind") or ""
        texts.append(text)
        key = None
        if "blink" in text.lower():
            key = "blink"
        elif "loses" in text and "life" in text:
            key = "drain"
        elif "deals" in text and "opponent" in text:
            key = "ping"                                  # Shivan Gorge
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
    t["events"] = tally
    t["n_events"] = len(texts)
    t["event_texts"] = texts[-40:]
    # THE SINGLE MOST DIAGNOSTIC NUMBER: blinks PROMISED by the plan summary against blinks the
    # apply actually RAN.  31 promised and 0 run is the loop never starting, which is a different
    # bug from "the finish ran out of mana".
    t["blinks_run"] = tally.get("blink", 0)
    pm = re.search(r"x(\d+)", plan_summary or "")
    t["blinks_promised"] = int(pm.group(1)) if pm else None
    return t


def _sweep_trace(t):
    """combo_off_sweep.mechanism() reads trace['goff'] / ['finish'] / ['events'] /
    ['blinks_run'] / ['blinks_promised']; hand it exactly that shape (empty when untraced)."""
    return t or {}


# ---------------------------------------------------------------------------------------------
# POPULATION 1 -- the user's saved reference games (READ-ONLY; never written, never reverted).
# ---------------------------------------------------------------------------------------------
def reference_walk(path):
    """Probe every aligned main-phase frame of one reference, branching at each combo-off offer."""
    c = {}
    try:
        ok, kind, detail = vpc.check_reference(path, collect=c)
    except Exception as exc:                                   # noqa: BLE001 -- report, never die
        return [], {"path": path, "walk": "EXCEPTION", "detail": repr(exc)}
    out = []
    meta = {"path": os.path.relpath(path, ROOT), "walk": kind, "detail": detail,
            "frames": len(c.get("frames") or []), "anomalies": []}
    if not ok:
        meta["anomalies"].append("viewer_protocol_check reports a CONTRACT failure: " + detail)
    if kind in ("play", "unresolvable", "mulligan", "shuffle-dead"):
        meta["anomalies"].append("replay of this reference is %s -- the probed frames are the "
                                 "REPLAYED line, not byte-for-byte the saved one" % kind)
    res = c.get("resolved") or []
    frames = c.get("frames") or []
    if not frames:
        return out, meta
    ref = json.load(open(path))
    ctx = {"seed": ref["seed"], "gi": ref["game_index"], "force": c.get("force"),
           "side": c.get("side"), "mt": c.get("mt", 8), "pop": "reference",
           "ref": os.path.basename(path)}
    # ONE persistent child replays the whole resolved stream and hands back every frame it passed
    # through -- including the ones check_reference answered by default, which the `frames` list
    # (aligned frames only) does not carry.  The terminal it reaches is the honest oracle for
    # "did this line win that turn": a REPAIRED replay can differ from the recording, and using
    # the recording's win turn would credit the engine with a win the replayed line never made.
    seen, line_win_turn = [], None
    s = Session(ctx["seed"], ctx["gi"], ctx["force"], ctx["side"], ctx["mt"])
    try:
        for i in range(len(res) + 8):
            kind, obj = s.read()
            if kind == "result":
                line_win_turn = obj.get("win_turn")
                meta["replay_won"] = bool(obj.get("won"))
                break
            if kind == "eof":
                meta["anomalies"].append("replay ended without a terminal after %d picks: %s"
                                         % (i, str(obj)[-160:]))
                break
            if obj.get("type") == "main_phase":
                seen.append((list(s.picks), compact_frame(obj)))
            s.send(res[i] if i < len(res)
                   else (-1 if obj.get("type") == "main_phase" else vpc.engine_default(obj)[0]))
    finally:
        s.close()
    meta["replay_win_turn"] = line_win_turn
    meta["recorded_win_turn"] = ref.get("win_turn")
    meta["walked_main_frames"] = len(seen)
    if line_win_turn != ref.get("win_turn"):
        meta["anomalies"].append("replay win_turn %s != recorded %s"
                                 % (line_win_turn, ref.get("win_turn")))
    meta["frames_with_float"] = sum(1 for _p, d in seen
                                    if (d.get("me") or {}).get("floating_mana"))
    for prefix, dec in seen:
        r = probe_frame(ctx, prefix, dec, line_win_turn,
                        stateless_argv(ctx["seed"], ctx["gi"], prefix, ctx["force"],
                                       ctx["side"], ctx["mt"]))
        r["ref"] = os.path.basename(path)
        r["recorded_win_turn"] = ref.get("win_turn")
        out.append(r)
    # The SAVED game's own float, for the side-by-side that makes the replay divergence legible.
    meta["recorded_frames_with_float"] = sum(
        1 for e in ref.get("decisions", [])
        if (e.get("decision") or {}).get("type") == "main_phase"
        and ((e["decision"].get("me") or {}).get("floating_mana")))
    meta["recorded_main_frames"] = sum(
        1 for e in ref.get("decisions", [])
        if (e.get("decision") or {}).get("type") == "main_phase")
    return out, meta


# ---------------------------------------------------------------------------------------------
# POPULATION 2 -- driven lines on fresh seeds.
# ---------------------------------------------------------------------------------------------
def drive_pick(dec):
    """Which plan this harness's driver commits at a main-phase frame.

    THIS IS NOT THE ENGINE'S PICK, and the protocol offers no way to ask for one (see the module
    docstring's PROTOCOL NOTE: a main_phase frame carries no heuristic_default / ai_choice, and -1
    means pass).  The policy is chosen to reach the states this hunt is ABOUT rather than to model
    the search: commit the plan that does the MOST -- casts, then a land drop, then board
    activations -- so the line assembles the combo and walks into the blink loop's re-prompts,
    where the user's failures live.  Combo-off plans are excluded from the DRIVING line because
    clicking one ends the game; they are branched into separately, which is the whole point.

    Deterministic and index-tie-broken, so a re-run probes the same states.
    """
    plans = dec.get("plans") or []
    best, best_key = -1, None
    for p in plans:
        if p.get("combo_off"):
            continue
        key = (len(p.get("casts") or []), 1 if p.get("land") else 0, len(p.get("actions") or []))
        if best_key is None or key > best_key:
            best, best_key = p["index"], key
    return best if best >= 0 else -1


def driven_walk(seed, gi, max_turns, limit_frames=None):
    """Walk one fresh game forward, probing every main-phase frame that offers a combo-off plan.

    Two passes, because "did the continuation win that turn" is only knowable at the terminal:
    pass 1 drives the line and remembers every frame's prefix; pass 2 probes them.
    """
    ctx = {"seed": seed, "gi": gi, "force": None, "side": None, "mt": max_turns,
           "pop": "autonomous"}
    seen, meta = [], {"seed": seed, "gi": gi, "anomalies": []}
    win_turn, npicks = None, 0
    s = Session(seed, gi, None, None, max_turns, DRIVEN_ENV)
    try:
        for _ in range(MAX_DRIVEN_DECISIONS):
            kind, obj = s.read()
            if kind == "result":
                win_turn = obj.get("win_turn")
                meta["won"] = bool(obj.get("won"))
                break
            if kind == "eof":
                meta["anomalies"].append("driven line ended without a terminal after %d picks: %s"
                                         % (len(s.picks), str(obj)[-160:]))
                break
            if obj.get("type") == "main_phase":
                seen.append((list(s.picks), compact_frame(obj)))
                s.send(drive_pick(obj))
            else:
                s.send(vpc.engine_default(obj)[0])
        else:
            meta["anomalies"].append("driven line hit the %d-decision cap" % MAX_DRIVEN_DECISIONS)
        npicks = len(s.picks)
    finally:
        s.close()
    meta["decisions"] = npicks
    meta["win_turn"] = win_turn
    meta["main_frames"] = len(seen)
    meta["frames_with_float"] = sum(1 for _p, d in seen
                                    if (d.get("me") or {}).get("floating_mana"))
    out_recs = []
    if limit_frames:
        seen = seen[:limit_frames]
    for prefix, dec in seen:
        out_recs.append(probe_frame(ctx, prefix, dec, win_turn,
                                    stateless_argv(seed, gi, prefix, None, None, max_turns)))
    return out_recs, meta


# ---------------------------------------------------------------------------------------------
# Repro verification -- a command that does not reproduce is worse than no command.
# ---------------------------------------------------------------------------------------------
def verify_repro(rec):
    """Re-run the recorded repro and confirm it still lands the verdict it is being cited for.

    A repro that does not reproduce is worse than no repro, so nothing is printed or catalogued
    until it has been re-run here.  The assertion is per class:
      class b -- the click must still NOT win,
      class a -- the click must still win,
      class c -- the frame must still come back a main_phase decision with NO combo-off plan.
    """
    cls = rec["class"]
    cmd = (rec.get("repro_terminal") or rec.get("repro")) \
        if cls in ("a_offered_wins", "b_executor_failure") else rec.get("frame_repro")
    if not cmd:
        return None
    p = subprocess.run(["/bin/sh", "-c", cmd], capture_output=True, text=True, cwd=ROOT)
    out = p.stdout + p.stderr
    res, dec = result_of(out), decision_of(out)
    won_tt = (isinstance(res, dict) and bool(res.get("won"))
              and res.get("win_turn") == rec["turn"])
    if cls == "b_executor_failure":
        return {"ok": (p.returncode in (0, 70)) and not won_tt, "rc": p.returncode,
                "won_this_turn": won_tt}
    if cls == "a_offered_wins":
        return {"ok": won_tt, "rc": p.returncode, "won_this_turn": won_tt}
    if cls in ("c_missed_offer", "c_missed_offer_combat"):
        ok = (isinstance(dec, dict) and dec.get("type") == "main_phase"
              and not combo_plans(dec))
        return {"ok": ok, "rc": p.returncode,
                "still_absent": (isinstance(dec, dict) and not combo_plans(dec))}
    return {"ok": p.returncode in (0, 70), "rc": p.returncode}


# ---------------------------------------------------------------------------------------------
# Driver.
# ---------------------------------------------------------------------------------------------
def summarise(records):
    """The derived fields + counts a finished result set carries.  Split out of main() so the
    `--finalise` path produces a byte-comparable file from a partial snapshot."""
    # MISSED-OFFER SUB-SPLIT.  `classify()` is kept verbatim, but a class-c frame on a turn the
    # line went on to WIN is not automatically a rule defect: the early frames of a go-off turn
    # are the turn ASSEMBLING the combo, and the button correctly does not fire until the loop
    # exists.  Split them, so the worklist carries only the frames where the offer NEVER fired on
    # a turn that was nevertheless won.
    by_turn = {}
    for r in records:
        by_turn.setdefault((r["population"], r["seed"], r["gi"], r["turn"]), []).append(r)
    for _key, g in by_turn.items():
        g.sort(key=lambda r: r["prefix_len"])
        first_offer = next((i for i, r in enumerate(g) if r["offered"]), None)
        any_offer = first_offer is not None
        for i, r in enumerate(g):
            r["turn_had_an_offer"] = any_offer
            r["offer_came_later_this_turn"] = bool(any_offer and first_offer > i)
            if r["class"] == "c_missed_offer":
                r["c_kind"] = ("c_before_first_offer" if r["offer_came_later_this_turn"]
                               else "c_never_offered_that_turn")
    classes = {}
    for r in records:
        classes[r["class"]] = classes.get(r["class"], 0) + 1
        if r.get("c_kind"):
            classes[r["c_kind"]] = classes.get(r["c_kind"], 0) + 1
    clusters = {}
    for r in records:
        if r.get("cluster"):
            clusters[r["cluster"]] = clusters.get(r["cluster"], 0) + 1
    return classes, clusters


def finalise(partial_path, out):
    """Promote a `<out>.partial` snapshot into a real result set.

    The hunt dumps a partial every ten completed jobs precisely so a run that has to be CUT SHORT
    still yields everything it finished.  This path adds the derived fields and the counts, and
    records honestly which jobs are in it -- it never invents the ones that are not.
    """
    d = json.load(open(partial_path))
    records = d["states"]
    classes, clusters = summarise(records)
    pops = {}
    for r in records:
        pops.setdefault(r["population"], set()).add((r["seed"], r["gi"]))
    payload = {
        "generated": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "commit": subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True,
                                 cwd=ROOT).stdout.strip(),
        "binary": os.path.relpath(MTG, ROOT),
        "finalised_from_partial": {"jobs_done": d.get("done"), "jobs_total": d.get("of")},
        "games": {k: sorted(v) for k, v in pops.items()},
        "classes": classes, "clusters": clusters,
        "references": [], "driven": [],
        "states": records,
    }
    with open(out, "w") as f:
        json.dump(payload, f, indent=1, sort_keys=True)
    print("finalised %d states from %d/%d jobs -> %s"
          % (len(records), d.get("done"), d.get("of"), os.path.relpath(out, ROOT)))
    for k in sorted(classes):
        print("  %-26s %5d" % (k, classes[k]))
    return 0


def _goff_lines(text):
    return sw.GOFF_RE.findall(text)


def _finish_counts(text):
    fs = sw.FINSTAT_RE.findall(text)
    if not fs:
        return None
    g = fs[-1]
    keys = ("draw_seen", "draw_guard", "draw_paid", "calls", "hand", "wish",
            "no_mana", "none_found", "pay_fail")
    return dict(zip(keys, (int(x) for x in g)))


def retrace(path, workers=10, out=None):
    """EXACT, FRAME-LOCAL engine instruments for every class-b state.

    The in-run trace is honest but blunt, and the two instruments are blunt in different ways:

      * `[edf-goff]` is printed by the go-off COUNT SIZER at ENUMERATION time and is capped at 40
        lines per process (MTG_EDF_GOFF_DEBUG_N).  A replay enumerates at every frame, so on a
        100-decision game the first 40 lines are all from turn 1-2 and say nothing about the frame
        being asked about -- exactly the trap the flag's own comment warns of.
      * `[finish]` is dumped at EXIT and is CUMULATIVE over the process, so a replay's counters
        cover the prefix as well as the click.

    Both are fixed by differencing two runs of the same prefix:

        pass A = prefix only            -> ends by EMITTING the probed frame, so the LAST
                                           `[edf-goff]` line is that frame's own sizing, and the
                                           finish counters are everything BEFORE the click;
        pass B = prefix + the click     -> finish counters including the click.

    The frame's goff line is A's last; the click's own finish work is B - A.  With
    MTG_EDF_GOFF_DEBUG_N raised past any real line count, neither is truncated.
    """
    d = json.load(open(path))
    st = [r for r in d["states"] if r["class"] in ("b_executor_failure", "d_rule_too_loose")]
    env = {"MTG_EDF_GOFF_DEBUG": "1", "MTG_EDF_FINISH_STATS": "1",
           "MTG_EDF_GOFF_DEBUG_N": "1000000"}

    def one(rec):
        choices, extra = _parse_repro(rec.get("frame_repro") or "")
        force = None
        if "--force-mulligan" in extra:
            i = extra.index("--force-mulligan")
            force = extra[i + 1]
            extra = extra[:i] + extra[i + 2:]
        # pass A -- prefix only, stopping ON the probed frame
        sA = Session(rec["seed"], rec["gi"], force, extra, 8, env)
        try:
            for p in choices:
                kind, obj = sA.read()
                if kind != "decision":
                    return rec["id"], None
                sA.send(p)
            kind, obj = sA.read()
        finally:
            sA.close()
        gl = _goff_lines(sA.err_text)
        fa = _finish_counts(sA.err_text) or {}
        # pass B -- the same prefix plus the click
        tr = click(rec["seed"], rec["gi"], force, extra, choices, rec["plan_index"], 8,
                   env_extra=env, want_raw=True)
        t = parse_trace(tr, rec.get("plan_summary"))
        fb = _finish_counts(tr.get("_raw", "")) or {}
        if fb:
            t["finish"] = {k: max(0, fb.get(k, 0) - fa.get(k, 0)) for k in fb}
            t["finish_cumulative"] = fb
            t["finish_before_click"] = fa
        if gl:
            g = gl[-1]
            t["goff"] = dict(outlet=g[1], payload=g[2], ok=g[3] == "1", net=int(g[4]),
                             refund=int(g[5]), cost=int(g[6]), gorge_dmg=int(g[7]),
                             gorge_cost=int(g[8]), drain_amount=int(g[9]),
                             drain_cost=int(g[10]), exile_cost=int(g[11]), kmax=int(g[12]),
                             afford=int(g[13]), iterations=int(g[14]))
            t["goff_lines_at_frame"] = len(gl)
        return rec["id"], t

    print("[retrace] %d class-b states, %d workers" % (len(st), workers), flush=True)
    by_id = {}
    with futures.ThreadPoolExecutor(max_workers=workers) as ex:
        for sid, t in ex.map(one, st):
            if t:
                by_id[sid] = t
    # Re-derive mechanism + cluster on the corrected trace.
    fixed = 0
    for r in d["states"]:
        if r["id"] not in by_id:
            continue
        r["trace"] = by_id[r["id"]]
        srec = {"offer": {"rule": r.get("rule"), "opp_lib_after": None,
                          "opp_life_after": (r.get("click") or {}).get("opp_life")},
                "arith": {"loop": {"net_c": r.get("loop_net_c"), "ok": r.get("loop_ok"),
                                   "bank": r.get("_bank", 0)},
                          "nC": r.get("_nC", 1), "emiel": r.get("_emiel", True),
                          "needs": {}, "has_red": True, "cheapest_need": None},
                "trace": r["trace"], "wish_in_lib_only": r.get("wish_in_lib_only"),
                "finisher_reachable": r.get("finisher_reachable"),
                "fixture": r.get("_fixture") or {"battlefield": [], "hand": []}}
        # mechanism() reads the fixture for the {C}-pip and damage-sink questions; rebuild the
        # minimal one it needs from the recorded board rather than storing a whole fixture.
        srec["fixture"] = {"battlefield": [{"name": n.split(" (T)")[0].split("+")[0]}
                                           for n in r["board"]["battlefield"]],
                           "hand": list(r["board"]["hand"])}
        before = r.get("cluster")
        r["mechanism"] = sw.mechanism(srec)
        r["cluster"] = cluster_of(r["mechanism"])
        fixed += 1
        if before != r["cluster"]:
            r["cluster_was"] = before
    moved = sum(1 for r in d["states"] if r.get("cluster_was"))
    print("[retrace] refreshed %d traces; %d states changed cluster" % (fixed, moved))
    clusters = {}
    for r in d["states"]:
        if r.get("cluster"):
            clusters[r["cluster"]] = clusters.get(r["cluster"], 0) + 1
    d["clusters"] = clusters
    d["retraced"] = True
    for k in sorted(clusters, key=lambda x: -clusters[x]):
        print("   %-24s %4d" % (k, clusters[k]))
    if out:
        with open(out, "w") as f:
            json.dump(d, f, indent=1, sort_keys=True)
        print("wrote %s" % os.path.relpath(out, ROOT))
    return 0


def _parse_repro(cmd):
    """(choices list, extra args) out of one of this harness's own frame repro strings."""
    import shlex
    toks = shlex.split(cmd)
    toks = [t for t in toks if "=" not in t.split("/")[0] or t.startswith("-") or "/" in t]
    choices, extra, i = [], [], 0
    while i < len(toks):
        if toks[i] == "--choices":
            choices = [int(x) for x in toks[i + 1].split(",") if x != ""]
            i += 2
        elif toks[i] in ("--force-mulligan", "--tap-pref", "--cast-order", "--force-attackers",
                         "--firebreathe", "--storage-hold"):
            extra += [toks[i], toks[i + 1]]
            i += 2
        else:
            i += 1
    return choices, extra


def float_offer_probe(path, only_class=("c_missed_offer",), workers=10, out=None):
    """DOES A FLOATING POOL CREATE OFFERS THE EMPTY-POOL SWEEP MISSES?

    The in-run float arms stage the pool inside the go-off's own apply, which cannot change what
    the MENU said -- `combo_off` is decided when the frame is ENUMERATED, before any pick.  To
    move the menu the pool has to exist BEFORE the frame, and the only honest way to arrange that
    (it is also the only way a human can) is to over-tap on the PREVIOUS committed line and let
    the surplus ride into the re-prompt: mana empties at end of step/phase (CR 500.4,
    GameEngine::CombatPhase), not between two committed lines in the same main phase.

    So for each selected state this re-runs its IMMEDIATELY PRECEDING main-phase frame with every
    hand-tappable source pre-tapped (`tap=` on --cast-order, position 0), commits the same pick
    the walk committed, and re-reads THIS frame -- then reports whether `combo_off` appeared that
    was not there before.  A frame whose predecessor is not a main_phase in the same turn+phase
    is skipped and says so: a pool cannot survive into it, so the question does not arise.
    """
    d = json.load(open(path))
    st = [r for r in d["states"] if r["class"] in only_class]
    by_key = {}
    for r in d["states"]:
        by_key[(r["population"], r["seed"], r["gi"], r["prefix_len"])] = r

    def one(rec):
        res = {"id": rec["id"], "population": rec["population"], "seed": rec["seed"],
               "gi": rec["gi"], "turn": rec["turn"], "phase": rec["phase"],
               "class": rec["class"], "staged": False}
        choices, extra = _parse_repro(rec.get("frame_repro") or "")
        if not choices:
            res["why"] = "first frame of the game -- nothing precedes it to float from"
            return res
        prev = by_key.get((rec["population"], rec["seed"], rec["gi"], len(choices) - 1))
        if prev is None or prev["turn"] != rec["turn"] or prev["phase"] != rec["phase"]:
            res["why"] = ("the preceding decision is not a main_phase in the same turn+phase, "
                          "so no pool can survive into this frame (CR 500.4)")
            return res
        if prev.get("main_ordinal") is None:
            res["why"] = "the preceding frame carries no main_ordinal to key --cast-order by"
            return res
        force = None
        if "--force-mulligan" in extra:
            force = extra[extra.index("--force-mulligan") + 1]
            extra = [t for i, t in enumerate(extra)
                     if i not in (extra.index("--force-mulligan"),
                                  extra.index("--force-mulligan") + 1)]
        # Re-read the PREVIOUS frame for its live tap faces + copy ids.
        rc, out_s, _ = run(rec["seed"], rec["gi"], choices[:-1], force, extra, 8)
        pdec = decision_of(out_s)
        if not isinstance(pdec, dict) or pdec.get("type") != "main_phase":
            res["why"] = "could not re-read the preceding frame"
            return res
        for arm, only_c in (("float_all", False), ("float_c", True)):
            toks = tap_tokens(pdec, only_c=only_c)
            if not toks:
                res[arm] = {"staged": False, "why": "no hand-tappable source on the prior board"}
                continue
            ex2 = list(extra) + ["--cast-order",
                                 "%d:%s" % (prev["main_ordinal"], "|".join(toks))]
            rc2, o2, argv2 = run(rec["seed"], rec["gi"], choices, force, ex2, 8)
            dec2 = decision_of(o2)
            if not isinstance(dec2, dict):
                res[arm] = {"staged": False, "why": "re-read after staging returned no decision"}
                continue
            cos = combo_plans(dec2)
            res["staged"] = True
            res[arm] = {
                "staged": True, "taps": toks,
                "float_now": (dec2.get("me") or {}).get("floating_mana"),
                "same_frame": (dec2.get("turn") == rec["turn"]
                               and dec2.get("phase") == rec["phase"]),
                "offered_now": bool(cos),
                "verified_now": bool(cos and cos[0].get("combo_off_verified")),
                "rule_now": cos[0].get("combo_off_rule") if cos else None,
                "n_plans": len(dec2.get("plans") or []),
                "NEW_OFFER": bool(cos) and not rec["offered"],
                "repro": repro_cmd(argv2),
            }
        return res

    print("[float-offer] probing %d states with %d workers" % (len(st), workers), flush=True)
    rows = []
    with futures.ThreadPoolExecutor(max_workers=workers) as ex:
        for r in ex.map(one, st):
            rows.append(r)
    new = [r for r in rows for a in ("float_all", "float_c")
           if isinstance(r.get(a), dict) and r[a].get("NEW_OFFER")]
    print("== FLOAT-CREATES-AN-OFFER ==")
    print("  selected       : %d" % len(rows))
    print("  stageable      : %d" % sum(1 for r in rows if r["staged"]))
    print("  NEW offers     : %d" % len({r["id"] for r in new}))
    for r in rows:
        if not r["staged"]:
            continue
        for a in ("float_all", "float_c"):
            v = r.get(a)
            if isinstance(v, dict) and v.get("staged"):
                print("   %s %-9s float=%-18s same_frame=%s offered=%s (was %s)"
                      % (r["id"], a, v.get("float_now"), v.get("same_frame"),
                         v.get("offered_now"), False))
    why = {}
    for r in rows:
        if not r["staged"]:
            why[r.get("why", "?")] = why.get(r.get("why", "?"), 0) + 1
    for k, v in sorted(why.items(), key=lambda kv: -kv[1]):
        print("   [%d] not stageable: %s" % (v, k))
    if out:
        with open(out, "w") as f:
            json.dump({"source": path, "rows": rows}, f, indent=1, sort_keys=True)
        print("wrote %s" % os.path.relpath(out, ROOT))
    return 0


def report(path):
    """Re-read a results.json and print every table the write-up needs.  Pure analysis: it runs
    no engine, so it can be re-run against an old result set for a before/after diff."""
    d = json.load(open(path))
    st = d["states"]

    def tab(title, rows, cols):
        print("\n== %s ==" % title)
        for r in rows:
            print("  " + "  ".join(str(c) for c in r))
        if cols:
            print("  (%s)" % cols)

    pops = sorted({r["population"] for r in st})
    order = ["a_offered_wins", "b_executor_failure", "c_missed_offer", "c_missed_offer_combat",
             "d_rule_too_loose", "e_absent_unwinnable", "x_error"]
    rows = []
    for k in order:
        n = sum(1 for r in st if r["class"] == k)
        per = "  ".join("%s=%d" % (p, sum(1 for r in st
                                          if r["class"] == k and r["population"] == p))
                        for p in pops)
        if n:
            rows.append(("%-24s" % k, "%5d" % n, "%5.1f%%" % (100.0 * n / max(1, len(st))), per))
    tab("CLASS TABLE (n=%d)" % len(st), rows, "class  n  share  per-population")

    offered = [r for r in st if r["offered"]]
    rows = []
    for rule in sorted({(r.get("rule") or "(empty)") for r in offered}):
        g = [r for r in offered if (r.get("rule") or "(empty)") == rule]
        w = sum(1 for r in g if r.get("click", {}).get("won_this_turn"))
        rows.append(("%-12s" % rule, "%4d offered" % len(g), "%4d win" % w,
                     "%5.1f%%" % (100.0 * w / len(g))))
    tab("PER-RULE", rows, "rule  offered  wins  rate")

    rows = []
    for v in (True, False):
        g = [r for r in offered if bool(r.get("verified")) == v]
        w = sum(1 for r in g if r.get("click", {}).get("won_this_turn"))
        if g:
            rows.append(("verified=%-5s" % v, "%4d" % len(g), "%4d win" % w,
                         "%5.1f%%" % (100.0 * w / len(g))))
    tab("VERIFIED AS AN ORACLE", rows, "flag  offered  wins  rate")

    clusters = {}
    for r in st:
        if r.get("cluster"):
            clusters.setdefault(r["cluster"], []).append(r)
    tab("CLUSTERS (class b)",
        [("%-22s" % k, "%4d" % len(v), ",".join(sorted({x["population"] for x in v})))
         for k, v in sorted(clusters.items(), key=lambda kv: -len(kv[1]))],
        "cluster  n  populations")

    fl = [r for r in offered if r.get("float")]
    changed = [(r["id"], a, f) for r in fl for a, f in r["float"].items()
               if f.get("staged") and f.get("changed_win")]
    print("\n== FLOAT-STAGED ARMS ==")
    print("  offered states with a float arm : %d" % len(fl))
    for arm in ("float_all", "float_c"):
        g = [r["float"][arm] for r in fl if r.get("float", {}).get(arm, {}).get("staged")]
        ap_ = sum(1 for x in g if (x.get("taps_applied") or 0) > 0)
        print("  %-10s staged=%3d  taps-actually-applied=%3d  win-changed=%d"
              % (arm, len(g), ap_, sum(1 for x in g if x.get("changed_win"))))
    for cid, arm, f in changed[:20]:
        print("    CHANGED %s %s: won=%s (plain differs)" % (cid, arm, f.get("won")))

    miss = [r for r in st if r.get("c_kind") == "c_never_offered_that_turn"]
    print("\n== MISSED OFFERS (class c, combat kills split out) ==")
    print("  %d class-c total: %d NEVER offered that turn (the real signal), %d before the "
          "turn's first offer (the turn was still assembling -- not a rule defect); "
          "%d combat-lethal split out"
          % (sum(1 for r in st if r["class"] == "c_missed_offer"), len(miss),
             sum(1 for r in st if r.get("c_kind") == "c_before_first_offer"),
             sum(1 for r in st if r["class"] == "c_missed_offer_combat")))
    for r in miss[:40]:
        print("   %s %s t%s %s ord=%s  wish_in_lib_only=%s finisher_reachable=%s float=%s"
              % (r["id"], r.get("ref") or "seed%s" % r["seed"], r["turn"], r["phase"],
                 r.get("main_ordinal"), r.get("wish_in_lib_only"), r.get("finisher_reachable"),
                 bool(r["board"].get("floating_mana"))))

    print("\n== ANOMALIES ==")
    for m in d.get("references", []) + d.get("driven", []):
        for a in m.get("anomalies", []):
            print("   %s: %s" % (m.get("path") or "seed %s" % m.get("seed"), a))
    seen = {}
    for r in st:
        for a in r.get("anomalies", []):
            seen.setdefault(a.split(":")[0], []).append(r["id"])
    for k, v in sorted(seen.items(), key=lambda kv: -len(kv[1])):
        print("   [%d states] %s  (e.g. %s)" % (len(v), k, v[0]))

    print("\n== FLOAT IN THE PROTOCOL ==")
    print("  probed frames carrying a non-empty pool: %d / %d"
          % (sum(1 for r in st if r["board"].get("floating_mana")), len(st)))
    for m in d.get("references", []):
        print("   %-46s replay frames w/ float %3s   RECORDED %3s / %3s"
              % (m.get("path"), m.get("frames_with_float"),
                 m.get("recorded_frames_with_float"), m.get("recorded_main_frames")))

    bad = [r for r in st if r.get("repro_verified") and not r["repro_verified"]["ok"]]
    print("\nrepros that did NOT reproduce: %d" % len(bad))
    for r in bad[:10]:
        print("   %s %s %s" % (r["id"], r["class"], r["repro_verified"]))
    return 0


# =============================================================================================
# --first-fire -- THE GAP, MEASURED IN MAIN-PHASE DECISIONS
# =============================================================================================
#
# USER, 2026-09-11: *"The key here is that the sooner the Combo Off line kicks in the better. The
# reason is that the search should have an easy way to query it and be able to skip a huge chain
# of operations."*  And, bounding how early "as early as it correctly can" may be: *"I think it's
# fair that some of the pieces (those to generate mana at least) should be on board first. That
# said, most of the work happens after this point."*
#
# So the metric is NOT "does the button eventually appear" -- the a/b/c/e classification above
# already answers that, per state, and answers it well.  It is the DISTANCE, counted in
# main-phase decisions, between the first frame at which the rule COULD correctly have fired and
# the frame at which it did.  Every decision in that gap is a whole plan enumeration a search
# shortcut could have skipped.
#
# FOUR MARKS PER GAME, all counted as an index into that game's own list of walked main-phase
# frames (which is exactly "decisions"), with the frame's `main_ordinal` carried alongside for
# legibility:
#
#   engine   the first frame whose board holds the mana ENGINE -- outlet + untapper + the lands
#            they cycle, `loop.ok` in the recognizer's sense.  This is the user's floor: nothing
#            before it may fire, so it is the ORIGIN the gap is measured from.  Taken from the
#            python arithmetic oracle (combo_off_sweep.arith), deliberately NOT from the engine,
#            so the measurement does not grade the engine with its own ruler.
#   oracle   the first frame at which the TRIAL APPLY WINS.  Forced by re-walking the identical
#            line with MTG_COMBO_OFF_RULES=0 MTG_COMBO_OFF_PROJECT=0: with the rule table silent
#            `offered_idx` is -1, so `combo_off_offered` can only come from `combo_off_verified`
#            (src/ai/TurnSolver.cpp), i.e. an offer in that arm IS a trial win.  PROJECT=0 is
#            needed as well, because the cheap lethal projection gates whether the trial is paid
#            for at all; with it forced, every go-off-carrying plan gets a real apply.
#   rule     the first frame at which `combo_off` appears on a plan in the DEFAULT arm.
#   verified the first frame at which that offer also carries `combo_off_verified`.
#
# THE ORACLE HAS A FLOOR OF ITS OWN, and it has to be said out loud rather than buried: the trial
# can only run on a plan that CARRIES a multi-activation go-off, because `EnumerateMainPlans`
# short-circuits on `best_any >= 0`.  On a frame where `FlickerGoOffCount` sizes nothing there is
# no plan to apply, so neither the rule table nor the trial is ever consulted -- the state is
# invisible to BOTH arms.  `goff_sized` is recorded per frame precisely so that population is
# countable, because it is the one place where "the rule is silent" and "the rule was never
# asked" look identical from outside and have completely different fixes.
ORACLE_ENV = {"MTG_COMBO_OFF_RULES": "0", "MTG_COMBO_OFF_PROJECT": "0"}


def ff_read_frame(dec):
    """The handful of bits --first-fire needs from one live main-phase frame.

    Read BEFORE `compact_frame` would drop the plan list, because `goff_sized` is a question
    about the plans that are NOT combo-off ones.
    """
    plans = dec.get("plans") or []
    goff = False
    for p in plans:
        for a in (p.get("actions") or []):
            if a.get("verb") == "blink" and (a.get("x") or 0) > 3:
                goff = True
                break
        if goff:
            break
    cos = [p for p in plans if p.get("combo_off")]
    ver = any(p.get("combo_off_verified") for p in cos)
    rule = next((p.get("combo_off_rule") for p in cos if p.get("combo_off_rule")), None)
    srec = sweep_record(dec, bool(cos), ver, rule, None, None, None, None)
    loop = ((srec or {}).get("arith") or {}).get("loop") or {}
    b = board_summary(dec)
    return {
        "main_ordinal": dec.get("main_ordinal"),
        "turn": dec.get("turn"),
        "phase": dec.get("phase"),
        "n_plans": len(plans),
        "goff_sized": goff,
        "offered": bool(cos),
        "verified": ver,
        "rule": rule,
        "loop_ok": bool(loop.get("ok")),
        "loop_net": loop.get("net"),
        "loop_net_c": loop.get("net_c"),
        "pool": b.get("floating_mana"),
        "library_size": b.get("library_size"),
    }


def ff_drive(seed, gi, force, side, mt, picks, env_extra):
    """Walk one line (a fixed pick stream, or the driven policy when `picks` is None)."""
    frames, win_turn, anomalies = [], None, []
    s = Session(seed, gi, force, side, mt, env_extra)
    try:
        for i in range(MAX_DRIVEN_DECISIONS if picks is None else len(picks) + 8):
            kind, obj = s.read()
            if kind == "result":
                win_turn = obj.get("win_turn")
                break
            if kind == "eof":
                anomalies.append("walk ended without a terminal after %d picks" % len(s.picks))
                break
            if obj.get("type") == "main_phase":
                frames.append(ff_read_frame(obj))
                if picks is None:
                    s.send(drive_pick(obj))
                    continue
            if picks is None:
                s.send(vpc.engine_default(obj)[0])
            else:
                s.send(picks[i] if i < len(picks)
                       else (-1 if obj.get("type") == "main_phase"
                             else vpc.engine_default(obj)[0]))
    finally:
        s.close()
    return frames, win_turn, anomalies


def ff_game(kind, payload, max_turns):
    """Both arms of one game.  Returns the per-game first-fire record."""
    if kind == "ref":
        c = {}
        try:
            vpc.check_reference(payload, collect=c)
        except Exception as exc:                               # noqa: BLE001 -- report, never die
            return {"game": os.path.basename(payload), "population": "reference",
                    "error": repr(exc)}
        ref = json.load(open(payload))
        seed, gi = ref["seed"], ref["game_index"]
        force, side, mt = c.get("force"), c.get("side"), c.get("mt", 8)
        picks = c.get("resolved") or []
        name, pop = os.path.basename(payload), "reference"
    else:
        seed, gi = payload
        force, side, mt, picks = None, None, max_turns, None
        name, pop = "seed %d gi %d" % (seed, gi), "autonomous"
    dflt, dwin, danom = ff_drive(seed, gi, force, side, mt, picks, None)
    orc, owin, oanom = ff_drive(seed, gi, force, side, mt, picks, ORACLE_ENV)
    rec = {"game": name, "population": pop, "seed": seed, "gi": gi,
           "frames": len(dflt), "win_turn": dwin, "oracle_win_turn": owin,
           "anomalies": danom + ["oracle: " + a for a in oanom],
           "default": dflt, "oracle": orc}
    if kind == "ref":
        rec["recorded_win_turn"] = ref.get("win_turn")
    # The two arms must walk the SAME line, or a gap is a comparison of two different games.
    # Non-go-off plans keep their indices whatever the rule table says (every go-off plan is
    # stripped and at most one re-APPENDED, src/ai/TurnSolver.cpp), so this should hold exactly;
    # it is asserted rather than assumed because a silent divergence would be invisible.
    if len(orc) != len(dflt):
        rec["anomalies"].append("arms walked different lengths: default %d, oracle %d"
                                % (len(dflt), len(orc)))
    else:
        drift = [i for i, (a, b) in enumerate(zip(dflt, orc))
                 if (a["main_ordinal"], a["turn"], a["library_size"], a["pool"])
                 != (b["main_ordinal"], b["turn"], b["library_size"], b["pool"])]
        if drift:
            rec["anomalies"].append("arms diverged at frame(s) %s" % drift[:5])

    def first(rows, key):
        return next((i for i, f in enumerate(rows) if f.get(key)), None)

    marks = {
        "engine": first(dflt, "loop_ok"),
        "sized": first(dflt, "goff_sized"),
        "rule": first(dflt, "offered"),
        "verified": first(dflt, "verified"),
        "oracle": first(orc, "offered"),          # rules off => offered iff the trial won
    }
    rec["mark"] = marks
    rec["ordinal"] = {k: (dflt[v]["main_ordinal"] if (v is not None and v < len(dflt)) else None)
                      for k, v in marks.items()}
    rec["rule_name"] = (dflt[marks["rule"]]["rule"] if marks["rule"] is not None else None)
    rec["gap_from_engine"] = (None if marks["engine"] is None or marks["rule"] is None
                              else marks["rule"] - marks["engine"])
    rec["gap_verified_from_engine"] = (None if marks["engine"] is None
                                       or marks["verified"] is None
                                       else marks["verified"] - marks["engine"])
    rec["gap_from_oracle"] = (None if marks["oracle"] is None or marks["rule"] is None
                              else marks["rule"] - marks["oracle"])
    # How many frames sat between the engine landing and the first fire with NO go-off plan sized
    # at all -- i.e. frames on which ComboOffPossible was never even called.
    if marks["engine"] is not None:
        end = marks["rule"] if marks["rule"] is not None else len(dflt)
        rec["unsized_in_gap"] = sum(1 for f in dflt[marks["engine"]:end] if not f["goff_sized"])
        rec["frames_after_engine"] = len(dflt) - marks["engine"]
    return rec


def ff_report(games, title):
    """Per-game table plus the mean/median gap per population."""
    print("\n=== FIRST FIRE: %s ===" % title)
    hdr = ("%-26s %4s %6s %6s %6s %6s %6s  %5s %5s %5s  %-10s"
           % ("game", "frms", "engine", "sized", "oracle", "rule", "verif",
              "gapE", "gapV", "gapO", "rule"))
    for pop in ("reference", "autonomous"):
        rows = [g for g in games if g.get("population") == pop and not g.get("error")]
        if not rows:
            continue
        print("\n-- %s (%d games) --" % (pop, len(rows)))
        print(hdr)

        def cell(v):
            return "-" if v is None else str(v)

        for g in sorted(rows, key=lambda r: r["game"]):
            m, o = g["mark"], g["ordinal"]

            def mo(k):
                return "-" if m[k] is None else "%d/%s" % (m[k], cell(o[k]))
            print("%-26s %4d %6s %6s %6s %6s %6s  %5s %5s %5s  %-10s"
                  % (g["game"][:26], g["frames"], mo("engine"), mo("sized"), mo("oracle"),
                     mo("rule"), mo("verified"), cell(g.get("gap_from_engine")),
                     cell(g.get("gap_verified_from_engine")), cell(g.get("gap_from_oracle")),
                     g.get("rule_name") or "-"))
        for label, key in (("gap: first fire - engine on board", "gap_from_engine"),
                           ("gap: first VERIFIED fire - engine", "gap_verified_from_engine"),
                           ("gap: first fire - first trial win", "gap_from_oracle")):
            vals = sorted(g[key] for g in rows if g.get(key) is not None)
            if not vals:
                print("  %-36s  (no game has both marks)" % label)
                continue
            mean = sum(vals) / len(vals)
            mid = (vals[len(vals) // 2] if len(vals) % 2
                   else (vals[len(vals) // 2 - 1] + vals[len(vals) // 2]) / 2.0)
            print("  %-36s n=%2d  mean %6.2f  median %5.1f  max %3d"
                  % (label, len(vals), mean, mid, vals[-1]))
        never = [g["game"] for g in rows if g["mark"]["engine"] is not None
                 and g["mark"]["rule"] is None]
        if never:
            print("  engine on board and NEVER offered: %d  %s" % (len(never), never[:6]))
        uns = [g.get("unsized_in_gap") for g in rows if g.get("unsized_in_gap") is not None]
        if uns:
            print("  frames in the gap with NO go-off plan sized (rule never consulted): "
                  "total %d over %d games" % (sum(uns), len(uns)))
    anom = [(g["game"], a) for g in games for a in g.get("anomalies", [])]
    if anom:
        print("\nanomalies: %d" % len(anom))
        for gname, a in anom[:12]:
            print("   %-26s %s" % (gname[:26], a))


def first_fire(args):
    """Run the first-fire measurement over both populations and print the table."""
    if not os.path.exists(MTG):
        print("ERROR: %s not found -- build Release first (./build.sh)." % MTG, file=sys.stderr)
        return 2
    skip = {s.strip() for s in args.skip.split(",") if s.strip()}
    lo, hi = (13, 24) if args.quick else (13, 72)
    if args.seeds:
        lo, hi = (int(x) for x in args.seeds.split(":"))
    max_turns = min(args.max_turns, 6) if args.quick else args.max_turns
    jobs = []
    if "reference" not in skip:
        refs = sorted(p for p in os.listdir(REFDIR)
                      if p.startswith("claude_s") and p.endswith(".json")
                      and args.ref_filter in p)
        jobs += [("ref", os.path.join(REFDIR, p)) for p in refs]
    if "autonomous" not in skip:
        jobs += [("seed", (s, s - 1)) for s in range(lo, hi + 1)]
    print("[first-fire] %d games, 2 arms each, %d workers" % (len(jobs), args.workers))
    t0 = time.time()
    games, done = [], 0
    # ONE pooled queue over both populations and (inside ff_game) both arms -- no per-arm wave.
    with futures.ThreadPoolExecutor(max_workers=args.workers) as ex:
        futs = {ex.submit(ff_game, k, p, max_turns): (k, p) for k, p in jobs}
        for fu in futures.as_completed(futs):
            g = fu.result()
            games.append(g)
            done += 1
            print("  [%3d/%3d] %-30s frames=%3s gapE=%-5s (%.0fs)"
                  % (done, len(jobs), g["game"][:30], g.get("frames"),
                     g.get("gap_from_engine"), time.time() - t0), flush=True)
    out = args.out if args.out != os.path.join(OUTDIR, "results.json") \
        else os.path.join(OUTDIR, "first_fire.json")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    payload = {
        "generated": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "commit": subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True,
                                 cwd=ROOT).stdout.strip(),
        "binary": os.path.relpath(MTG, ROOT),
        "oracle_env": ORACLE_ENV,
        "seeds": [lo, hi],
        "wall_s": round(time.time() - t0, 1),
        "games": games,
    }
    with open(out, "w") as f:
        json.dump(payload, f, indent=1, sort_keys=True)
    ff_report(games, os.path.relpath(out, ROOT))
    print("\nwrote %s  (%.0fs)" % (os.path.relpath(out, ROOT), time.time() - t0))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--report", default=None,
                    help="analyse an existing results.json and print every table (runs no engine)")
    ap.add_argument("--finalise", default=None,
                    help="promote a <out>.partial snapshot (written every 10 jobs) into a real "
                         "results.json -- the path for a run that had to be cut short")
    ap.add_argument("--retrace", default=None,
                    help="second pass over an existing results.json: re-derive every class-b "
                         "state's engine instruments FRAME-LOCALLY (two-pass differencing)")
    ap.add_argument("--float-offer", default=None,
                    help="second pass over an existing results.json: stage a floating pool on the "
                         "PRECEDING committed line and re-ask whether the button appears")
    ap.add_argument("--float-offer-class", default="c_missed_offer",
                    help="comma list of classes the --float-offer pass selects")
    ap.add_argument("--first-fire", action="store_true",
                    help="measure the GAP, in main-phase decisions, between the mana engine "
                         "landing on the battlefield / the trial apply first winning, and the "
                         "rule table first firing.  Two arms per game (default, and the trial "
                         "forced with MTG_COMBO_OFF_RULES=0 MTG_COMBO_OFF_PROJECT=0); no clicks")
    ap.add_argument("--quick", action="store_true",
                    help="gate mode, minutes: 4 references + 12 driven seeds, horizon 6")
    ap.add_argument("--seeds", default=None,
                    help="driven seeds, 'lo:hi' inclusive (default 13:72, quick 13:24)")
    ap.add_argument("--skip", default="", help="comma list of populations to skip "
                                               "(reference,autonomous)")
    ap.add_argument("--ref-filter", default="",
                    help="substring a reference filename must contain (debug/bisect aid)")
    ap.add_argument("--workers", type=int, default=12)
    ap.add_argument("--max-turns", type=int, default=8)
    ap.add_argument("--no-verify", action="store_true",
                    help="skip the repro re-run (the default re-runs every class-b/c repro)")
    ap.add_argument("--out", default=os.path.join(OUTDIR, "results.json"))
    args = ap.parse_args()

    if args.report:
        return report(args.report)
    if args.finalise:
        return finalise(args.finalise, args.out)
    if args.retrace:
        return retrace(args.retrace, workers=args.workers,
                       out=os.path.join(os.path.dirname(args.retrace), "results.retraced.json"))
    if args.float_offer:
        return float_offer_probe(
            args.float_offer,
            only_class=tuple(s for s in args.float_offer_class.split(",") if s),
            workers=args.workers,
            out=os.path.join(os.path.dirname(args.float_offer), "float_offer.json"))
    if args.first_fire:
        return first_fire(args)
    if not os.path.exists(MTG):
        print("ERROR: %s not found -- build Release first (./build.sh)." % MTG, file=sys.stderr)
        return 2
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    skip = {s.strip() for s in args.skip.split(",") if s.strip()}
    lo, hi = (13, 24) if args.quick else (13, 72)
    if args.seeds:
        lo, hi = (int(x) for x in args.seeds.split(":"))
    max_turns = args.max_turns
    if args.quick:
        # HORIZON 6, not 8, and it is the whole reason --quick is quick.  This deck's plan
        # enumeration blows up on a late developed board (turns 7-8 emit 7,500-11,500 plans, and
        # three turn-7/8 frames in the full run each burned >80 min of CPU inside ONE
        # enumeration -- see the HANG anomalies in docs/design/combo-off-replay-hunt.md).  Every
        # reference in the corpus wins on turn 3-6, so a horizon of 6 keeps the whole interesting
        # region and drops only the region that is slow AND uninteresting.
        max_turns = min(max_turns, 6)

    t0 = time.time()
    records, ref_meta, seed_meta = [], [], []
    jobs = []
    if "reference" not in skip:
        refs = sorted(p for p in os.listdir(REFDIR)
                      if p.startswith("claude_s") and p.endswith(".json")
                      and args.ref_filter in p)
        if args.quick:
            # The four SHORTEST recorded games (fewest decisions): full archetype coverage of the
            # rules that actually fire -- WISH-DRAW, DEPLOYED, GORGE -- at a fraction of the cost
            # of the 68-frame monsters.
            refs = sorted(refs, key=lambda p: len(
                json.load(open(os.path.join(REFDIR, p))).get("decisions", [])))[:4]
        jobs += [("ref", os.path.join(REFDIR, p)) for p in refs]
    if "autonomous" not in skip:
        for s in range(lo, hi + 1):
            jobs.append(("seed", (s, s - 1)))
    print("[hunt] %d jobs (%d references, %d seeds), %d workers"
          % (len(jobs), sum(1 for k, _ in jobs if k == "ref"),
             sum(1 for k, _ in jobs if k == "seed"), args.workers))

    # ONE pooled queue over every reference and every seed -- no per-population wave, so the box
    # drains to a single tail (CLAUDE.md's pooled-queue rule).
    def do(job):
        kind, payload = job
        if kind == "ref":
            return kind, reference_walk(payload)
        return kind, driven_walk(payload[0], payload[1], max_turns)

    done = 0
    with futures.ThreadPoolExecutor(max_workers=args.workers) as ex:
        futs = {ex.submit(do, j): j for j in jobs}
        for fu in futures.as_completed(futs):
            kind, (recs, meta) = fu.result()
            records += recs
            (ref_meta if kind == "ref" else seed_meta).append(meta)
            done += 1
            print("  [%3d/%3d] %-38s %3d frames, %2d offers  (%.0fs)"
                  % (done, len(jobs),
                     meta.get("path") or "seed %s gi %s" % (meta.get("seed"), meta.get("gi")),
                     len(recs), sum(1 for r in recs if r["offered"]), time.time() - t0),
                  flush=True)
            # Partial dump every few jobs: this run was OOM-killed once at 50/71 and lost
            # everything, which is a bad trade for one json.dump.
            if done % 10 == 0:
                with open(args.out + ".partial", "w") as f:
                    json.dump({"done": done, "of": len(jobs), "states": records}, f)

    # Verify every repro we are going to print.
    if not args.no_verify:
        need = [r for r in records
                if r["class"] in ("a_offered_wins", "b_executor_failure",
                                  "c_missed_offer", "c_missed_offer_combat")]
        print("[hunt] verifying %d repros" % len(need), flush=True)
        with futures.ThreadPoolExecutor(max_workers=args.workers) as ex:
            for r, v in zip(need, ex.map(verify_repro, need)):
                r["repro_verified"] = v

    # MISSED-OFFER SUB-SPLIT.  `classify()` is kept verbatim, but a class-c frame on a turn the
    # line went on to WIN is not automatically a rule defect: the early frames of a go-off turn
    # are the turn ASSEMBLING the combo, and the button correctly does not fire until the loop
    # exists.  Split them, so the worklist carries only the frames where the offer NEVER fired on
    # a turn that was nevertheless won.
    by_turn = {}
    for r in records:
        by_turn.setdefault((r["population"], r["seed"], r["gi"], r["turn"]), []).append(r)
    for key, g in by_turn.items():
        g.sort(key=lambda r: (r["prefix_len"]))
        first_offer = next((i for i, r in enumerate(g) if r["offered"]), None)
        any_offer = first_offer is not None
        for i, r in enumerate(g):
            r["turn_had_an_offer"] = any_offer
            r["offer_came_later_this_turn"] = bool(any_offer and first_offer > i)
            if r["class"] == "c_missed_offer":
                r["c_kind"] = ("c_before_first_offer" if r["offer_came_later_this_turn"]
                               else "c_never_offered_that_turn")

    classes = {}
    for r in records:
        classes[r["class"]] = classes.get(r["class"], 0) + 1
    for r in records:
        if r.get("c_kind"):
            classes[r["c_kind"]] = classes.get(r["c_kind"], 0) + 1
    clusters = {}
    for r in records:
        if r.get("cluster"):
            clusters[r["cluster"]] = clusters.get(r["cluster"], 0) + 1

    payload = {
        "generated": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "commit": subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True,
                                 cwd=ROOT).stdout.strip(),
        "binary": os.path.relpath(MTG, ROOT),
        "seeds": [lo, hi],
        "wall_s": round(time.time() - t0, 1),
        "classes": classes,
        "clusters": clusters,
        "references": ref_meta,
        "driven": seed_meta,
        "states": records,
    }
    with open(args.out, "w") as f:
        json.dump(payload, f, indent=1, sort_keys=True)

    print("\n=== COMBO OFF REPLAY HUNT ===")
    print("states probed : %d  (%d reference, %d driven)"
          % (len(records), sum(1 for r in records if r["population"] == "reference"),
             sum(1 for r in records if r["population"] == "autonomous")))
    for k in ("a_offered_wins", "b_executor_failure", "c_missed_offer",
              "c_missed_offer_combat", "d_rule_too_loose", "e_absent_unwinnable", "x_error"):
        if classes.get(k):
            print("  %-24s %5d" % (k, classes[k]))
    if clusters:
        print("clusters:")
        for k in sorted(clusters, key=lambda x: -clusters[x]):
            print("  %-28s %4d" % (k, clusters[k]))
    bad = [r for r in records if r.get("repro_verified") and not r["repro_verified"]["ok"]]
    if bad:
        print("REPROS THAT DID NOT REPRODUCE: %d" % len(bad))
    print("wrote %s  (%.0fs)" % (os.path.relpath(args.out, ROOT), time.time() - t0))
    return 0


if __name__ == "__main__":
    sys.exit(main())

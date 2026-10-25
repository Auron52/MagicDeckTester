#!/usr/bin/env python3
"""PROMOTE A SEARCH LINE TO A REFERENCE -- re-express the search's game as a play-viewer recording.

The review viewer (tools/replay/review.html) shows a reference game beside the shipped search's game.
When the user judges the search's line the better one, "Promote" makes it THE reference for that game:
the file the bench, the protocol sweep and the viewer's maturity badges all read.

A reference is a play-viewer DECISION STREAM (the plan index picked at each frame), while the search's
game is an autonomous action log, so promotion is a translation, done by playing the game again in
claude-play mode and choosing, at each main-phase frame, the enumerated plan that does what the search
did in that phase:

  * the human's recorded picks are replayed first when the source is a hand-off (`handoff_T<k>.json`:
    your turns 1..k-1, then the search), so the promoted game keeps your opening turns;
  * at each main-phase frame, among the plans that use only cards the search used in that phase, the
    one whose PROBED result (played on to the next main-phase frame) comes closest to the search's
    end-of-phase board (battlefield, hand, opponent life) is committed; pass when nothing gets closer.
    A plan that reaches the search's own win is taken at once (see walk() for why names alone fail).
    A cast at an X the search did not use is not the search's action: a plan's X is fixed from the mana
    in hand when it is enumerated, so a bigger X can need an earlier segment (lifegain for Accomplished
    Alchemist, SelesnyaLifegain s15_gi14, 2026-10-25);
  * every other frame (a target, a tutor pick, a scry, a put-into-play) is answered the same way: each
    single-pick answer is probed and the one whose board at the next main-phase frame is closest to
    the search's is taken (multi-pick frames take the engine's default). Answers that tie there are
    probed on to the next turn's first main phase: a card tutored to the top of the library is hidden
    until it is drawn (Worldly Tutor, StompySurprise s1_gi0, 2026-10-25);
  * the first point where the replay's board drifts from the search's is noted, and named in the
    refusal if the line then misses the search's win.

A promotion is written only if the result is a FAITHFUL, LITERAL reference: the engine-written trace
(the same writer the viewer's save uses) wins on exactly the search's win turn and round-trips through
test/viewer_protocol_check.check_reference as plain `ok`. Anything else is refused with the reason --
"replay it by hand in the play viewer" is then the route.

    python3 scripts/ref_promote.py treasure_hunt/claude_s5_gi4.json                 # dry run, search from T1
    python3 scripts/ref_promote.py treasure_hunt/claude_s5_gi4.json --source handoff_T4.json
    python3 scripts/ref_promote.py <key> --source <file> --apply --json             # what review_server runs
    python3 scripts/ref_promote.py --check-all                                      # dry-run every review game

--apply writes references/<deck>/claude_s<S>_gi<G>.json. The human's own line is NEVER lost: before it
is replaced, its exact bytes are copied to references/review/replaced/<deck>/human_s<S>_gi<G>.json, and
both files are listed in the JSON output for the caller to back up + commit + push (review_server.js does, through tools/play/refsafe.js), as is the
`promoted` verdict this records in references/review/verdicts.json.

WHY THAT PATH, and not references/suboptimal/: suboptimal/ is the user's note that the VIEWER would not
let them reach the optimal turn (a target the search must beat), not an archive of beaten lines. Under
references/ the copy is still covered by scripts/check_references_safe.py; the `human_` prefix keeps it
out of every `claude_s*_gi*.json` glob, so neither the bench nor the protocol sweep reads it as a live
reference.
"""
import argparse
import collections
import concurrent.futures
import datetime
import json
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
sys.path.insert(0, os.path.join(ROOT, "test"))
import ref_review as rr                      # noqa: E402
import viewer_protocol_check as vpc          # noqa: E402
import combo_off_replay_hunt as hunt         # noqa: E402

SCRATCH = os.path.join("build", "scratch", "ref_promote")
REPLACED = os.path.join("references", "review", "replaced")   # the human lines a promotion replaced
VERBOSE = False       # --verbose: every main-phase frame's probes and choice, to stderr


def say(*a):
    if VERBOSE:
        print(*a, file=sys.stderr, flush=True)
PHASE_OF = {"MAIN_1": "pre_main", "COMBAT": "pre_main", "MAIN_2": "post_main", "CLEANUP": "post_main"}
LEGACY_PREFIX = "--legacy-"
MAX_PROBES = 32       # candidate plans probed per frame (each probe is one game replay)


class Refused(Exception):
    pass


# ---- the search log ---------------------------------------------------------------------------

# Action kinds that are not something a plan does (draws, reveals, damage lines, discards forced by a
# resolving effect): every other named action is a card the search used that phase.
NOT_PLAYED = {"DRAW", "REVEAL", "ATTACK", "DISCARD"}


def phase_names(log, from_turn):
    """{(turn, phase): {'lands': set of land-drop names, 'cards': set of other card names,
    'count': Counter of 'land:<name>' / '<name>' per named action, 'xs': {name: set of X the search cast
    it with}} -- the cards the search used in each main phase. Combat-step casts (tricks) belong to the
    pre-combat plan, as the viewer's plans hold them."""
    out = collections.defaultdict(lambda: {"lands": set(), "cards": set(), "count": collections.Counter(),
                                           "xs": collections.defaultdict(set)})
    for s in log.get("turns") or []:
        ph = PHASE_OF.get(s.get("phase"))
        if ph is None or s.get("turn", 0) < from_turn:
            continue
        for a in s.get("actions") or []:
            n = a.get("cardName")
            if not n or a.get("type") in NOT_PLAYED:
                continue
            land = a.get("type") == "PLAY_LAND"
            out[(s["turn"], ph)]["lands" if land else "cards"].add(n)
            out[(s["turn"], ph)]["count"][("land:" if land else "") + n] += 1
            if a.get("type") == "CAST_SPELL" and isinstance(a.get("chosenX"), int):
                out[(s["turn"], ph)]["xs"][n].add(a["chosenX"])
    return out


def boards(log):
    """{(turn, phase): (board BEFORE the phase, board AFTER it)} from the log's step boards."""
    out, prev = {}, None
    for s in log.get("turns") or []:
        ph = PHASE_OF.get(s.get("phase"))
        if s.get("phase") in ("MAIN_1", "MAIN_2"):
            out[(s["turn"], ph)] = (prev, s.get("boardAfter"))
        prev = s.get("boardAfter")
    return out


def log_sig(b):
    """(battlefield names, hand card numbers, opponent life) of a log board."""
    if not b:
        return None
    return (collections.Counter(x.get("cardName") for x in b.get("battlefield") or []),
            collections.Counter(b.get("hand") or []), b.get("opponentLife"))


def frame_sig(d):
    """The same signature for a claude-play decision frame (or result frame)."""
    me, opp = d.get("me") or {}, d.get("opponent") or {}
    if isinstance(me, str):
        me = json.loads(me)
    if isinstance(opp, str):
        opp = json.loads(opp)
    return (collections.Counter(p.get("name") for p in me.get("battlefield") or []),
            collections.Counter(c.get("num") for c in me.get("hand") or [] if isinstance(c, dict)),
            opp.get("life"))


def sig_diff(a, b):
    """-> (distance, text) between two signatures."""
    bf = (a[0] - b[0]) + (b[0] - a[0])
    hd = (a[1] - b[1]) + (b[1] - a[1])
    life = abs((a[2] or 0) - (b[2] or 0)) if a[2] is not None and b[2] is not None else 0
    txt = []
    if a[0] != b[0]:
        txt.append("battlefield: replay has %s, search had %s" % (
            sorted((a[0] - b[0]).elements()) or "-", sorted((b[0] - a[0]).elements()) or "-"))
    if a[1] != b[1]:
        txt.append("hand: replay has #%s, search had #%s" % (
            sorted((a[1] - b[1]).elements()) or "-", sorted((b[1] - a[1]).elements()) or "-"))
    if life:
        txt.append("opponent life: replay %s, search %s" % (a[2], b[2]))
    return sum(bf.values()) + sum(hd.values()) + life, "; ".join(txt)


OPTION_LISTS = ("options", "candidates", "colors", "pool")
BINARY = {"vial_charge", "echo", "demonstrate", "land_entry"}


def option_picks(d):
    """The single-pick answers a non-main-phase frame offers, engine default first; [] when the frame
    is a multi-pick (a list `chosen`) or has nothing to choose between -- those take the default."""
    t = d.get("type")
    dflt = vpc.engine_default(d)[0]
    if t == "x_value" and isinstance(d.get("min_x"), int) and isinstance(d.get("max_x"), int):
        opts = list(range(d["min_x"], d["max_x"] + 1))
    elif t == "replicate" and isinstance(d.get("max_count"), int):
        opts = list(range(0, d["max_count"] + 1))
    elif t in BINARY:
        opts = [0, 1]
    else:
        lst = next((d[k] for k in OPTION_LISTS if isinstance(d.get(k), list)), None)
        opts = list(range(len(lst))) if lst else []
    if len(opts) < 2:
        return []
    return [dflt] + [o for o in opts if o != dflt]


def plan_ok(p, used):
    """A plan the search could have been playing: its land drop (if any) is one the search made this
    phase, and every card it uses is one the search used."""
    if p.get("land") and p["land"] not in used["lands"]:
        return False
    cards = {a.get("card") for a in p.get("actions") or [] if a.get("card")}
    return bool(cards or p.get("land")) and cards <= used["cards"]


def wrong_x(a, xs):
    """A cast at an X the search did not use for that card: not the search's action. A plan's X is fixed
    when the plan is enumerated, from the mana available THEN, so the search's bigger X can need an
    earlier segment first (SelesnyaLifegain s15_gi14, 2026-10-25: Feed the Clan + Wellwisher gain the
    life Accomplished Alchemist taps for, and only the next segment offers Genesis Wave X=14)."""
    if a.get("activate") or not xs.get(a.get("card")):
        return False
    return (a.get("x") or 0) not in xs[a["card"]]          # a plan omits "x" at X=0


def content(p, xs=None):
    c = collections.Counter(a.get("card") for a in p.get("actions") or []
                            if a.get("card") and not wrong_x(a, xs or {}))
    if p.get("land"):
        c["land:" + p["land"]] += 1
    return c


def progress(p, rest, xs=None):
    """(how many of the search's still-unplayed actions this phase the plan plays, minus its casts at an
    X the search did not use) -- compared as a tuple."""
    return (sum((content(p, xs) & rest).values()),
            -sum(wrong_x(a, xs or {}) for a in p.get("actions") or []))


# ---- side channels ------------------------------------------------------------------------------

def side_before(side, turn, ordinal):
    """The reference's keyed side-channel args, keeping only the entries for turns before `turn`
    (main ordinals before `ordinal`): the human's pins must steer the human's turns, never the
    search's. Value-less recording-rule flags (--legacy-*) are kept -- the whole game is played, and
    then re-recorded, under the rules the human's turns were recorded under."""
    turn_keyed = {"--firebreathe": ",", "--storage-hold": ",", "--jitte": ",",
                  "--force-attackers": ";", "--tap-pref": ";"}
    ord_keyed = {"--cast-order": ";", "--full-enum": ";"}
    out, i = [], 0
    while i < len(side):
        a = side[i]
        if a.startswith(LEGACY_PREFIX):
            out.append(a)
            i += 1
            continue
        if i + 1 >= len(side):
            raise Refused("unrecognised side-channel arg %r" % a)
        val = side[i + 1]
        if a in turn_keyed or a in ord_keyed:
            sep = (turn_keyed.get(a) or ord_keyed.get(a))
            lim = turn if a in turn_keyed else ordinal
            keep = [e for e in val.split(sep) if e and int(e.split(":", 1)[0]) < lim]
            if keep:
                out += [a, sep.join(keep)]
        else:
            raise Refused("side-channel %s has no known key, cannot split it at the hand-off" % a)
        i += 2
    return out


# ---- the walk -----------------------------------------------------------------------------------

class Driver:
    """A persistent `--claude-play --interactive` child, like hunt.Session, that SURVIVES the frames
    where interactive mode does not wait for more input. Some choosers end the process when the pick
    stream runs out instead of blocking on stdin (a Utopia Sprawl `choose_color` did, 2026-10-24), so
    the child exits with no result after the answer is sent. On that EOF the child is respawned with
    every pick so far PRE-FILLED in --choices; it replays them silently and continues interactively
    from the next frame. Same engine, same determinism; only the transport differs."""

    def __init__(self, c, extra):
        self.c, self.extra, self.picks = c, list(extra), []
        self._spawn()

    def _spawn(self):
        c = self.c
        argv = [vpc.MTG, c["deck"], "--claude-play", "--interactive", "--seed", str(c["seed"]),
                "--game-index", str(c["gi"]), "--max-turns", str(c["mt"]), "--depth", "0",
                "--profile", c["prof"], "--choices", ",".join(str(p) for p in self.picks)]
        if c["force"] is not None:
            argv += ["--force-mulligan", c["force"]]
        argv += self.extra
        env = dict(os.environ)
        env.update(hunt.REPLAY_ENV)
        env["MTG_VIEWER_PLAN_CAP_POSITIONS"] = str(c.get("valve") or vpc.VALVE_POSITIONS)
        self.err = tempfile.TemporaryFile(mode="w+")
        self.proc = subprocess.Popen(vpc.capped(argv), stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=self.err, text=True, cwd=ROOT, env=env, bufsize=1)
        self.sent_since_spawn = 0

    def read(self):
        for _ in range(2):
            buf = []
            while True:
                line = self.proc.stdout.readline()
                if not line:
                    break
                buf.append(line)
                if line.startswith("<<<END_DECISION>>>"):
                    d = hunt.decision_of("".join(buf))
                    return ("decision", d) if isinstance(d, dict) else ("eof", "malformed decision")
                if line.startswith("<<<END_RESULT>>>"):
                    r = hunt.result_of("".join(buf))
                    return ("result", r) if isinstance(r, dict) else ("eof", "malformed result")
            if not self.sent_since_spawn:          # it died on the stream it was GIVEN: a real stop
                return "eof", "".join(buf[-12:])
            self.close()
            self._spawn()                          # resume past the chooser that would not wait
        return "eof", "".join(buf[-12:])

    def send(self, pick):
        self.picks.append(int(pick))
        self.sent_since_spawn += 1
        try:
            self.proc.stdin.write("%d\n" % int(pick))
            self.proc.stdin.flush()
        except (BrokenPipeError, ValueError):
            pass

    def close(self):
        for f in (self.proc.stdin, self.proc.stdout):
            try:
                f.close()
            except Exception:                                    # noqa: BLE001
                pass
        try:
            self.proc.wait(timeout=20)
        except Exception:                                        # noqa: BLE001
            self.proc.kill()
        self.err.close()

def probe_on(c, extra, picks, want_win):
    """Play `picks`, then engine defaults through every non-main-phase frame, and stop at the next
    main-phase frame or the result: the board a candidate plan REALLY leaves, after its own follow-up
    choices (a Land's Edge plan's discards, a tutor's pick) have resolved. -> (kind, frame, won_like_log)."""
    s = Driver(c, extra)
    try:
        for _ in range(2000):
            kind, d = s.read()
            if kind == "result":
                return kind, d, bool(d.get("won")) and d.get("win_turn") == want_win
            if kind == "eof":
                return kind, None, False
            if len(s.picks) < len(picks):
                s.send(picks[len(s.picks)])
            elif d.get("type") == "main_phase":
                return kind, d, False
            else:
                s.send(vpc.engine_default(d)[0])
        return "eof", None, False
    finally:
        s.close()


def probe_next_turn(c, extra, picks, turn):
    """Play `picks`, then engine defaults at every other frame and PASS at every main phase, up to the
    first main-phase frame of a turn after `turn`. -> that frame, or None. The tie-break for answers
    whose boards look the same at the next main phase: a card tutored to the TOP of the library (Worldly
    Tutor) is invisible until it is drawn, so only the next turn's hand tells the answers apart."""
    s = Driver(c, extra)
    try:
        for _ in range(2000):
            kind, d = s.read()
            if kind != "decision":
                return None
            if len(s.picks) < len(picks):
                s.send(picks[len(s.picks)])
            elif d.get("type") == "main_phase":
                if (d.get("turn") or 0) > turn:
                    return d
                s.send(-1)
            else:
                s.send(vpc.engine_default(d)[0])
        return None
    finally:
        s.close()


def probe(c, extra, picks):
    """The frame (or result) after `picks`, from a fresh stateless replay."""
    rc, out = vpc.replay(c["deck"], c["prof"], c["seed"], c["gi"], picks, c["force"], extra, c["mt"],
                         valve=c.get("valve") or vpc.VALVE_POSITIONS)
    m = vpc.DEC_RE.search(out)
    if rc == 70 and m:
        return "decision", json.loads(m.group(1))
    r = hunt.result_of(out)
    return ("result", r) if isinstance(r, dict) else ("eof", out[-400:])


def walk(c, prefix, extra, log, from_turn):
    """-> (the full pick stream that plays `prefix` and then the search's line, notes). Raises Refused.

    Among the plans that use only cards the search used in that phase: while some of the search's
    named actions are still unplayed, the plan that plays the most of them (ties: the one whose probed
    result is closest to the search's END-OF-PHASE board -- battlefield, hand, opponent life); after
    that, repeat activations are hill-climbed on that distance, and the phase passes when nothing gets
    closer. A plan that reaches the search's own win is taken at once. Progress has to come first: a
    pure distance climb stalls in a local minimum (Treasure Hunt draws fifteen lands, moving the hand
    AWAY from the board Land's Edge later reaches). Card names alone cannot drive this: the
    autonomous log does not name every activation (21 Land's Edge discards are 21 two-damage lines),
    and the viewer splits one search phase into several segments (a land and a Treasure Hunt, then
    the Land's Edge it drew). Exact identity with the search's game is NOT required -- which land a
    discard picks is immaterial -- the gate is the outcome: the same win on the same turn."""
    names = phase_names(log, from_turn)
    bd = boards(log)
    res = log.get("result") or {}
    want_win = res.get("turn") if res.get("winner") == "player" else None
    s = Driver(c, extra)
    checked, notes, first_div, cur_key = set(), [], None, None
    rest = {k: collections.Counter(v["count"]) for k, v in names.items()}
    try:
        for _ in range(5000):
            kind, d = s.read()
            if kind == "result":
                won = d.get("won")
                if not won or d.get("win_turn") != want_win:
                    raise Refused("the replayed line ends %s, the search's game %s%s" % (
                        "T%s" % d.get("win_turn") if won else "without a win",
                        "T%s" % want_win if want_win else "without a win",
                        (" -- it first drifted from the search's game " + first_div) if first_div else ""))
                return list(s.picks), notes
            if kind == "eof":
                raise Refused("the replay stopped without a result: %s" % str(d)[-300:])
            if len(s.picks) < len(prefix):
                s.send(prefix[len(s.picks)])
                continue
            if d.get("type") != "main_phase":
                picks_ = option_picks(d)
                if not picks_:
                    s.send(vpc.engine_default(d)[0])
                    continue
                # A target, a tutor pick, a scry, a put-into-play: the answer whose board, once play
                # reaches the next main-phase frame, is closest to the search's at that point.
                best, ties = (10 ** 9, picks_[0]), []
                for o in picks_[:MAX_PROBES]:
                    k2, d2, win = probe_on(c, extra, list(s.picks) + [o], want_win)
                    if win:
                        best, ties = (-1, o), []
                        break
                    if k2 != "decision" or d2 is None:
                        continue
                    k2key = (d2.get("turn"), d2.get("phase"))
                    ref = bd.get(k2key) or (None, None)
                    tgt = log_sig(ref[1] if k2key == cur_key else ref[0])
                    if tgt is None:
                        continue
                    dist = sig_diff(frame_sig(d2), tgt)[0]
                    say("   %s option %s -> %d" % (d.get("type"), o, dist))
                    if dist < best[0]:
                        best, ties = (dist, o), [o]
                    elif dist == best[0]:
                        ties.append(o)
                # Answers that leave the same visible board (a card tutored to the top of the library)
                # are told apart at the start of the next turn, after the draw.
                if len(ties) > 1:
                    t = d.get("turn") or 0
                    deep = (10 ** 9, ties[0])
                    for o in ties:
                        d3 = probe_next_turn(c, extra, list(s.picks) + [o], t)
                        tgt = d3 and log_sig((bd.get((d3.get("turn"), d3.get("phase"))) or (None, None))[0])
                        if tgt is None:
                            continue
                        dist = sig_diff(frame_sig(d3), tgt)[0]
                        say("   %s option %s, at T%s -> %d" % (d.get("type"), o, d3.get("turn"), dist))
                        if dist < deep[0]:
                            deep = (dist, o)
                    best = (best[0], deep[1])
                s.send(best[1])
                continue
            key = (d.get("turn"), d.get("phase"))
            cur_key = key
            if key not in checked:
                checked.add(key)
                before = log_sig((bd.get(key) or (None, None))[0])
                if before is not None and first_div is None:
                    dist, why = sig_diff(frame_sig(d), before)
                    if dist:
                        first_div = "at the start of T%s %s (%s)" % (key[0], key[1], why)
                        notes.append("drifted " + first_div)
            used = names.get(key) or {"lands": set(), "cards": set(), "count": collections.Counter()}
            b0, b1 = bd.get(key) or (None, None)
            target = log_sig(b1)
            if target is None:
                s.send(-1)
                continue
            # A permanent the search had in play during the phase can be ACTIVATED even when the log
            # never names it (a cascaded-into Land's Edge discarding lands shows only as damage lines).
            used = dict(used, cards=used["cards"] | {x.get("cardName") for b in (b0, b1) if b
                                                     for x in b.get("battlefield") or []})
            cands = [p for p in d.get("plans") or [] if plan_ok(p, used)]
            if not cands:
                s.send(-1)
                continue
            left = rest.setdefault(key, collections.Counter())
            if +left:
                xs = used.get("xs") or {}
                top = max(progress(p, left, xs) for p in cands)
                if top[0] > 0:
                    cands = [p for p in cands if progress(p, left, xs) == top]
            # The plan must beat PASSING (`here`) only once the search's named actions are all played.
            here = sig_diff(frame_sig(d), target)[0]
            if here == 0 and not +left:
                s.send(-1)                       # already the search's board: nothing to probe
                continue
            best = (here if not +left else 10 ** 9, None)
            say("T%s %s: distance %d to the search's board, %d candidate(s)" % (key[0], key[1], here, len(cands)))
            for p in cands[:MAX_PROBES]:
                k2, d2, win = probe_on(c, extra, list(s.picks) + [p["index"]], want_win)
                if win:
                    best = (-1, p)
                    break
                if d2 is None or k2 != "decision":
                    continue
                dist = sig_diff(frame_sig(d2), target)[0]
                say("   [%d] %-70s -> %d" % (p["index"], p.get("summary", "")[:70], dist))
                if dist < best[0]:
                    best = (dist, p)
                    if dist == 0:
                        break
            if best[1] is None:
                say("   pass")
                s.send(-1)
                continue
            say("   take [%d]%s" % (best[1]["index"], " (reaches the win)" if best[0] < 0 else ""))
            rest[key] = left - content(best[1], used.get("xs") or {})
            if len(cands) > MAX_PROBES:
                notes.append("T%s %s: probed %d of %d candidate plans" % (key[0], key[1], MAX_PROBES, len(cands)))
            s.send(best[1]["index"])
        raise Refused("walked 5000 frames without a result")
    finally:
        s.close()


def write_trace(c, extra, picks, outdir):
    """Re-run the full pick stream with --log-dir: the ENGINE writes the reference file."""
    shutil.rmtree(outdir, ignore_errors=True)
    os.makedirs(outdir)
    args = [vpc.MTG, c["deck"], "--claude-play", "--seed", str(c["seed"]), "--game-index", str(c["gi"]),
            "--max-turns", str(c["mt"]), "--depth", "0", "--profile", c["prof"],
            "--choices", ",".join(str(p) for p in picks), "--log-dir", outdir]
    if c["force"] is not None:
        args += ["--force-mulligan", c["force"]]
    args += extra
    env = dict(os.environ, MTG_PLAY_PLANS_CAP="0",
               MTG_VIEWER_PLAN_CAP_POSITIONS=str(c.get("valve") or vpc.VALVE_POSITIONS))
    p = subprocess.run(vpc.capped(args), capture_output=True, text=True, cwd=ROOT, env=env)
    path = os.path.join(outdir, "claude_s%d_gi%d.json" % (c["seed"], c["gi"]))
    if p.returncode != 0 or not os.path.exists(path):
        raise Refused("writing the trace failed (rc=%d): %s" % (p.returncode, p.stderr[-300:]))
    return path


def roundtrip(cand_path, ref_dir):
    """check_reference on the candidate, resolved against the deck of `ref_dir` (references/<dir>)."""
    rel = os.path.dirname(os.path.relpath(cand_path, "references"))
    vpc.DECKS[rel] = vpc.DECKS[ref_dir]
    ok, kind, detail = vpc.check_reference(cand_path)
    if not ok or kind != "ok":
        raise Refused("the promoted line does not replay as a literal reference (%s: %s)" % (kind, detail))


# ---- promote ------------------------------------------------------------------------------------

def promote(key, source, apply_it, note=""):
    os.chdir(ROOT)
    idx = {e["key"]: e for e in rr.load_index()["games"]}
    if key not in idx:
        raise Refused("'%s' is not in the review index" % key)
    e = idx[key]
    gdir = os.path.join(rr.OUT, e["dir"])
    log_path = os.path.join(gdir, source)
    if not os.path.exists(log_path):
        raise Refused("no %s for this game" % source)
    log = json.load(open(log_path))
    if key.startswith("suboptimal/"):
        raise Refused("references/suboptimal/ games are not promoted (they are viewer-limit notes)")
    ref_path = os.path.join("references", key)
    ref_dir = os.path.dirname(key)

    c = {}
    ok, kind, detail = vpc.check_reference(ref_path, collect=c)
    if not ok or "resolved" not in c:
        raise Refused("your recorded game does not replay (%s: %s), so its turns cannot be kept" % (kind, detail))
    if source == "search.json":
        prefix, from_turn, extra = [], 1, []     # the bench game: current rules, no human pins at all
    else:
        rung = next((r for r in e["ladder"] if r.get("cv_log") == source), None)
        if rung is None:
            raise Refused("%s is not one of this game's hand-offs" % source)
        prefix, from_turn = c["resolved"][:rung["picks"]], rung["turn"]
        k, d = probe(c, c["side"], prefix)
        if k != "decision" or d.get("turn") != from_turn:
            raise Refused("could not reach your board at T%d" % from_turn)
        extra = side_before(c["side"], from_turn, d.get("main_ordinal", 10 ** 6))
    picks, notes = walk(c, prefix, extra, log, from_turn)

    work = os.path.join(SCRATCH, key[:-5])
    cand = write_trace(c, extra, picks, os.path.join(work, "trace"))
    got = json.load(open(cand))
    want = (log.get("result") or {}).get("turn")
    if not got.get("won") or got.get("win_turn") != want:
        raise Refused("the written trace ends T%s, the search's game T%s" % (got.get("win_turn"), want))
    rt = os.path.join(work, "roundtrip", "references", ref_dir)
    os.makedirs(rt, exist_ok=True)
    rt_path = os.path.join(rt, os.path.basename(cand))
    shutil.copyfile(cand, rt_path)
    roundtrip(rt_path, ref_dir)

    out = {"ok": True, "key": key, "source": source, "win_turn": want, "human_win": e["human_win"],
           "from_turn": from_turn, "notes": notes, "applied": False, "files": []}
    if apply_it:
        dest = ref_path
        # Keep the human's line, byte for byte, before it is replaced.
        keep = os.path.join(REPLACED, ref_dir, "human_s%d_gi%d.json" % (c["seed"], c["gi"]))
        if os.path.exists(keep):
            raise Refused("%s already exists; not overwriting it" % keep)
        os.makedirs(os.path.dirname(keep), exist_ok=True)
        shutil.copyfile(ref_path, keep)
        out["files"].append(keep)
        out["human_line_kept_at"] = keep
        stamp = {"source": source, "from_turn": from_turn, "human_win": e["human_win"],
                 "src": rr.src_tree(), "at": datetime.datetime.now(datetime.timezone.utc)
                 .isoformat(timespec="seconds")}
        text = open(cand).read()
        marker = ',\n  "decisions": ['
        if marker not in text:
            raise Refused("unexpected trace layout; not writing it")
        text = text.replace(marker, ',\n  "promoted_from_search": %s%s' % (json.dumps(stamp), marker), 1)
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with open(dest + ".tmp", "w") as fh:
            fh.write(text)
        os.replace(dest + ".tmp", dest)
        out["files"].append(dest)
        out["applied"], out["reference"] = True, dest
        v = rr.load_verdicts()
        v["verdicts"][key] = rr.stamp(e, "promoted", note, source=source, reference=dest,
                                      promoted_win=want, human_line_kept_at=out.get("human_line_kept_at"))
        rr.save_verdicts(v)
        out["files"].append(rr.VERDICTS)
    return out


def check_all(jobs):
    """Dry-run promotion of every review game's search line from T1 AND from its first lost turn."""
    games = rr.load_index()["games"]
    work = []
    for e in games:
        work.append((e["key"], "search.json"))
        lost = (e.get("lost_turns") or [{}])[0].get("turn")
        r = next((r for r in e["ladder"] if r["turn"] == lost and r.get("cv_log")), None)
        if r and lost > 1:
            work.append((e["key"], r["cv_log"]))

    def run(item):
        p = subprocess.run([sys.executable, os.path.abspath(__file__), item[0], "--source", item[1], "--json"],
                           capture_output=True, text=True, cwd=ROOT)
        try:
            return item, json.loads(p.stdout.strip().splitlines()[-1])
        except Exception:                                        # noqa: BLE001
            return item, {"ok": False, "error": (p.stderr or p.stdout)[-300:]}
    n_ok = 0
    reasons = collections.Counter()
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
        for (key, src), r in ex.map(run, work):
            n_ok += bool(r.get("ok"))
            if not r.get("ok"):
                reasons[(r.get("error") or "?").split("(")[0].split(":")[0][:60]] += 1
            print("%-4s %-55s %-18s %s" % ("ok" if r.get("ok") else "NO", key, src,
                                          "; ".join(r.get("notes") or []) if r.get("ok") else r.get("error")),
                  flush=True)
    print("\n%d of %d promotions would succeed" % (n_ok, len(work)))
    for k, v in reasons.most_common():
        print("  %3d  %s" % (v, k))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("key", nargs="?", help="review key, e.g. treasure_hunt/claude_s5_gi4.json")
    ap.add_argument("--source", default="search.json", help="search.json (default) or handoff_T<k>.json")
    ap.add_argument("--apply", action="store_true", help="write the reference (default: dry run)")
    ap.add_argument("--json", action="store_true", help="print one JSON result line")
    ap.add_argument("--note", default="", help="note stored with the 'promoted' verdict")
    ap.add_argument("--check-all", action="store_true", help="dry-run every game in the review index")
    ap.add_argument("--verbose", action="store_true", help="print every main-phase frame's probes (stderr)")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    args = ap.parse_args()
    global VERBOSE
    VERBOSE = args.verbose
    if args.check_all:
        return check_all(args.jobs)
    if not args.key:
        ap.error("a review key is required")
    try:
        out = promote(args.key, args.source, args.apply, args.note)
    except Refused as err:
        out = {"ok": False, "key": args.key, "source": args.source, "error": str(err)}
    if args.json:
        print(json.dumps(out))
    else:
        print(json.dumps(out, indent=1))
    return 0 if out["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())

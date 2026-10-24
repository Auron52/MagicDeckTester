#!/usr/bin/env python3
"""REFERENCE REVIEW -- every reference game the shipped search BEATS, laid out for a human verdict.

scripts/ref_bench.py asks "does the search fall short of the human?". This asks the reverse: on which
saved references does the search win EARLIER than the human did, and is that because the search's line
is genuinely better (a human misplay worth learning from) or because the search is CLAIRVOYANT (it plans
against the real library order, which a human cannot see)? Only the user can settle that, so this tool
gathers the evidence and the user settles each game in the review viewer (tools/replay/review.html,
served by ./review.sh) or from the command line: PROMOTE the search's line to be the reference
(scripts/ref_promote.py), mark it CLAIRVOYANCE (the line needs to know the library) or a BUG (the line
relies on an engine defect), or mark it REPLAY MANUALLY (your line is beatable, but you will find the
better line yourself in the play viewer).

    python3 scripts/ref_review.py build                  # find + localise + lay out every such game
    python3 scripts/ref_review.py build --deck burn snow # a subset (deck_registry slugs)
    python3 scripts/ref_review.py status                 # the table, with each game's verdict
    python3 scripts/ref_review.py mark treasure_hunt/claude_s10_gi9.json clairvoyance --note "..."
    python3 scripts/ref_promote.py treasure_hunt/claude_s5_gi4.json --apply    # promote the search's line

WHAT `build` DOES, in three steps:
  1. FIND. Every reference in references/ is played by the shipped search from the human's exact
     opening hand, in ONE pooled
     `mtg --batch` -- ref_bench.py's own collect/run/report, so the numbers are the bench's numbers.
     A game is kept when the search wins strictly earlier, or wins where the human did not.
     references/suboptimal/ is NOT reviewed (USER 2026-10-24): a game there is the user's own note
     that the VIEWER would not let them reach the optimal turn, so the search beating it is the
     expected outcome (ref_bench.py --suboptimal --require-beat checks exactly that), not a question
     about the user's line.
  2. LOCALISE. For each kept game the human's recorded line is replayed to the start of every one of
     their turns and handed to the shipped search there (`--choices-then-auto`, the same hand-off as
     scripts/ref_handoff_sweep.py). The search's win turn from "your board at the start of turn T" is
     the LADDER; a rung where it gets worse (T -> T+1) is a turn on which your line lost ground,
     because the only thing between the two boards is how you played turn T.
  3. HINT. The same ladder is run again under MTG_NC_SEARCH=1, the engine's NON-clairvoyant search
     (plans against reshuffled unseen libraries, executes against the real one). If a blind search
     also gains on a turn you lost ground on, the better line did not need foresight: hint
     `likely-misplay`. If it does not, the gain needed to know the library: `likely-clairvoyance`.
     It is a HINT -- the NC search is a weaker, experimental policy -- never the verdict.

Everything lands in logs/ref_review/ (gitignored, recreatable):
    index.json, INDEX.md
    <ref dir>/<game>/review.json      the ladder, hints, fidelity checks, repro commands
    <ref dir>/<game>/human.json       YOUR game, as a replay-viewer game log (exact boards; replaying a
                                      choice stream does not log the plays, so the review page reads
                                      them from reference.json's decisions)
    <ref dir>/<game>/search.json      the search's game from turn 1 (the bench's own trace)
    <ref dir>/<game>/handoff_T<k>.json  your turns 1..k-1, then the search from your board at turn k
    <ref dir>/<game>/reference.json   a COPY of the reference (the original is never touched)

VERDICTS ARE USER WORK and live in a TRACKED file, references/review/verdicts.json, so the reference
safety scanner (scripts/check_references_safe.py) and the viewer's autosave protect them exactly like a
hand-played game. Each verdict is stamped with the search line it judged (`search_digest`); when an
engine change alters that line, the verdict is shown as STALE rather than silently carried over.
"""
import argparse
import collections
import datetime
import glob
import hashlib
import json
import os
import queue
import shutil
import subprocess
import sys
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
sys.path.insert(0, os.path.join(ROOT, "test"))
import deck_registry                         # noqa: E402
import ref_bench                             # noqa: E402
import ref_handoff_sweep as rhs              # noqa: E402  -- enumerate_frames
import viewer_protocol_check as vpc          # noqa: E402
import combo_off_replay_hunt as hunt         # noqa: E402  -- REPLAY_ENV, result_of, pasteable_cmd

MTG = os.environ.get("MTG_BIN", os.path.join(ROOT, "build", "Release", "mtg"))
OUT = os.path.join("logs", "ref_review")
SCRATCH = os.path.join("build", "scratch", "ref_review")   # per-job engine output (CLAUDE.md: volume)
VERDICTS = os.path.join("references", "review", "verdicts.json")
# The verdicts `mark` (and the viewer's buttons) record. "promoted" is the fourth outcome, written only by
# scripts/ref_promote.py --apply, because it is an action (the search's line becomes the reference), not
# a label. "replay_manually" is a to-do marker: the user agrees their line is beatable but not with the
# search's line, and will re-save the game by hand in the play viewer.
VERDICT_KINDS = ("clairvoyance", "bug", "replay_manually")
# Per-process cache budget for the hand-off pool. The default is MemTotal/2 PER PROCESS, which with a
# full pool of them is several times the box; the budget only sizes caches, it does not change a line.
POOL_MEM_MB = os.environ.get("MTG_REVIEW_MEM_MB", "1000")


# ---- verdicts -----------------------------------------------------------------------------

def load_verdicts():
    try:
        with open(os.path.join(ROOT, VERDICTS)) as fh:
            return json.load(fh)
    except FileNotFoundError:
        return {"schema": 1, "verdicts": {}}


def save_verdicts(v):
    path = os.path.join(ROOT, VERDICTS)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w") as fh:
        json.dump(v, fh, indent=1, sort_keys=True)
        fh.write("\n")
    os.replace(tmp, path)


def verdict_state(entry, verdict):
    """'unreviewed' | 'current' | 'stale' -- stale when the search line it judged has since changed."""
    if not verdict:
        return "unreviewed"
    return "current" if verdict.get("search_digest") == entry.get("search_digest") else "stale"


# ---- helpers ------------------------------------------------------------------------------

def line_digest(log):
    """Fingerprint of a game log's LINE (what was played each step), for verdict staleness."""
    sig = [(t.get("turn"), t.get("phase"),
            [(a.get("type"), a.get("cardName") or a.get("card")) for a in t.get("actions") or []])
           for t in log.get("turns") or []]
    return hashlib.sha1(json.dumps(sig).encode()).hexdigest()[:16]


def ref_key(path):
    """A reference's identity: its path relative to references/ (e.g. 'burn/claude_s3_gi2.json')."""
    return os.path.relpath(path, os.path.join(ROOT, "references")).replace(os.sep, "/")


def src_tree():
    p = subprocess.run(["git", "rev-parse", "HEAD:src"], capture_output=True, text=True, cwd=ROOT)
    return p.stdout.strip()


def turn_starts(frames, last_turn):
    """The first main-phase frame of each turn up to `last_turn` -- 'your board at the start of T'."""
    out, seen = [], set()
    for f in frames:
        t = f.get("turn")
        if t is None or t in seen or t > last_turn:
            continue
        seen.add(t)
        out.append(f)
    return out


_SRC = None


def cache_key(argv, env):
    """A finished engine game is a pure function of its argv, the env that steers it, and the engine
    (src tree + the binary's bytes, by size and mtime) -- so a rerun of `build` skips it."""
    global _SRC
    if _SRC is None:
        st = os.stat(MTG)
        _SRC = "%s:%d:%d" % (src_tree(), st.st_size, int(st.st_mtime))
    keep = {k: v for k, v in env.items() if k.startswith("MTG_")}
    return hashlib.sha1(json.dumps([_SRC, argv, sorted(keep.items())]).encode()).hexdigest()


def engine(c, prefix, log_dir=None, env_kv=()):
    """One claude-play game: the recorded picks `prefix`, then the shipped search plays on.

    With the FULL resolved stream as the prefix the search never takes over, so the --log-dir game log
    is the human's own game. Same argv + REPLAY_ENV as ref_handoff_sweep.run_handoff, so a hand-off
    here is that tool's hand-off, plus the game log."""
    argv = [MTG, c["deck"], "--claude-play", "--seed", str(c["seed"]), "--game-index", str(c["gi"]),
            "--max-turns", str(c["mt"]), "--profile", c["prof"],
            "--choices", ",".join(str(p) for p in prefix), "--choices-then-auto"]
    if c["force"] is not None:
        argv += ["--force-mulligan", c["force"]]
    argv += list(c["side"] or [])
    if log_dir:
        os.makedirs(log_dir, exist_ok=True)
        argv += ["--log-dir", log_dir]
    env = dict(os.environ)
    env.update(hunt.REPLAY_ENV)
    if c.get("valve"):
        env["MTG_VIEWER_PLAN_CAP_POSITIONS"] = str(c["valve"])
    env.setdefault("MTG_MEM_BUDGET_MB", POOL_MEM_MB)
    for kv in env_kv:
        k, _, v = kv.partition("=")
        env[k] = v
    key = cache_key([a for a in argv if a != log_dir], env)
    cdir = os.path.join(SCRATCH, "cache", key[:2], key)
    try:
        with open(os.path.join(cdir, "result.json")) as fh:
            out = json.load(fh)
        if log_dir:
            out["log"] = os.path.join(cdir, "game.json") if os.path.exists(os.path.join(cdir, "game.json")) else None
        out["cached"] = True
        return out
    except (FileNotFoundError, ValueError):
        pass
    t0 = time.time()
    p = subprocess.run(vpc.capped(argv), capture_output=True, text=True, cwd=ROOT, env=env)
    res = hunt.result_of(p.stdout)
    out = {"rc": p.returncode, "secs": round(time.time() - t0, 1),
           "cmd": hunt.pasteable_cmd(argv, list(env_kv))}
    if isinstance(res, dict):
        out["won"] = bool(res.get("won"))
        out["win"] = res.get("win_turn") if res.get("won") else None
    else:
        out["error"] = "\n".join(p.stderr.splitlines()[-6:]) or "no result frame (rc=%d)" % p.returncode
    if log_dir:
        logs = glob.glob(os.path.join(log_dir, "then_auto_s*_game.json"))
        out["log"] = logs[0] if logs else None
    if "error" not in out:                  # never cache a failure: it may be the box, not the game
        os.makedirs(cdir, exist_ok=True)
        if out.get("log"):
            shutil.copyfile(out["log"], os.path.join(cdir, "game.json"))
        with open(os.path.join(cdir, "result.json"), "w") as fh:
            json.dump({k: v for k, v in out.items() if k != "log"}, fh)
    return out


# ---- step 1: find ------------------------------------------------------------------------

def find(args):
    """-> (candidates, totals). ref_bench's own pooled run over references/ (never suboptimal/)."""
    decks = deck_registry.discover()
    want = {deck_registry.slug(d) for d in args.deck} if args.deck else None
    bench_args = argparse.Namespace(max_turns=args.max_turns, depth=None, threads=args.threads,
                                    suboptimal=False)
    entries = []
    for slug, refdir in ref_bench.ref_dirs("references"):        # skips references/suboptimal/ itself
        if want and slug not in want:
            continue
        key = deck_registry.reference_deck_key(slug)
        if key not in decks:
            print("SKIPPED %s: no deck folder for '%s'" % (refdir, key))
            continue
        e = ref_bench.collect_deck(key, refdir, decks[key], bench_args)
        e["refdir"] = refdir
        if e["games"]:
            entries.append(e)
    trace_dir = os.path.join(SCRATCH, "bench")
    shutil.rmtree(trace_dir, ignore_errors=True)
    os.makedirs(trace_dir)
    wins = ref_bench.run_pooled(entries, bench_args, trace_dir)
    cands, totals = [], {"games": 0, "faster": 0, "tied": 0, "short": 0, "hand_bad": 0}
    for e in entries:
        ref_bench.report_deck(e, wins, bench_args, trace_dir)
        for g in e["games"]:
            totals["games"] += 1
            h, s = g["human"], g["search"]
            if g["hand"].startswith("HAND"):
                totals["hand_bad"] += 1
                continue
            if isinstance(s, int) and (h is None or s < h):
                totals["faster"] += 1
                name = ref_bench.job_name(e["deck"], g)
                cands.append({"path": os.path.join(ROOT, e["refdir"], g["name"]),
                              "deck_key": e["deck"],
                              "seed": g["seed"], "gi": g["gi"], "human_win": h, "search_win": s,
                              "bench_trace": os.path.join(trace_dir, "%s_gi%d.json" % (name, g["gi"]))})
            elif isinstance(s, int) and s == h:
                totals["tied"] += 1
            else:
                totals["short"] += 1
    return cands, totals


# ---- step 2+3: localise + hint -------------------------------------------------------------

SESSION_LOCK = threading.Lock()   # hunt.Session reads the module globals DECK/PROF when it starts


def enumerate_starts(cand, c):
    """Walk the resolved pick stream once (one persistent child) and keep the first main-phase frame
    of each turn: 'your board at the start of T'. Same walk as ref_handoff_sweep.enumerate_frames,
    with the Session construction under a lock so references can be walked concurrently."""
    res = c["resolved"]
    with SESSION_LOCK:
        hunt.DECK, hunt.PROF = c["deck"], c["prof"]
        s = hunt.Session(c["seed"], c["gi"], c["force"], c["side"], c["mt"])
    last = cand["human_win"] if cand["human_win"] is not None else c["mt"]
    starts, seen = [], set()
    try:
        for i in range(len(res) + 8):
            kind, obj = s.read()
            if kind != "decision":
                break
            t = obj.get("turn")
            if obj.get("type") == "main_phase" and t is not None and t not in seen and t <= last:
                seen.add(t)
                starts.append({"turn": t, "phase": obj.get("phase"), "frame": 0,
                               "prefix": list(s.picks), "board": rhs.board_line(obj)})
            s.send(res[i] if i < len(res)
                   else (-1 if obj.get("type") == "main_phase" else vpc.engine_default(obj)[0]))
    finally:
        s.close()
    return starts


def lost_rungs(cand):
    """The ladder turns the non-clairvoyant hint needs: each turn T where your line lost ground, and
    the rung after it (your board one turn later). Needs only this game's clairvoyant ladder."""
    mt = cand["max_turns"]
    score = lambda w: w if isinstance(w, int) else mt + 1
    turns = [f["turn"] for f in cand["starts"]]
    cv = {t: score(((cand.get("ladder_raw") or {}).get(t) or {}).get("cv", {}).get("win")) for t in turns}
    need = set()
    for i, t in enumerate(turns):
        nxt = cv[turns[i + 1]] if i + 1 < len(turns) else score(cand["human_win"])
        if nxt > cv[t]:
            need.add(t)
            if i + 1 < len(turns):
                need.add(turns[i + 1])
    return sorted(need)


def hint(cand):
    """Lost turns from the clairvoyant ladder, and what the non-clairvoyant ladder says about each."""
    mt = cand["max_turns"]
    score = lambda w: w if isinstance(w, int) else mt + 1     # no win scores horizon+1, as in the bench
    final = score(cand["human_win"])
    rungs = cand["ladder"]
    lost = []
    for i, r in enumerate(rungs):
        if "cv_win" not in r or r.get("cv_error"):
            continue
        nxt = rungs[i + 1] if i + 1 < len(rungs) else None
        cv_next = score(nxt["cv_win"]) if nxt and "cv_win" in nxt else final
        if cv_next > score(r["cv_win"]):
            row = {"turn": r["turn"], "cv_from": score(r["cv_win"]), "cv_after": cv_next}
            if "nc_win" in r and not r.get("nc_error"):
                nc_next = score(nxt["nc_win"]) if nxt and "nc_win" in nxt else final
                row.update(nc_from=score(r["nc_win"]), nc_after=nc_next)
            lost.append(row)
    cand["lost_turns"] = lost
    if not rungs or any(r.get("cv_error") for r in rungs):
        return "unclear", "the hand-off ladder has errors; see review.json"
    if not lost:
        return "unclear", ("no turn of your line loses ground in the hand-off ladder (the search from "
                           "turn 1 wins T%s here vs the bench's T%s)" % (rungs[0].get("cv_win"),
                                                                       cand["search_win"]))
    with_nc = [x for x in lost if "nc_from" in x]
    if not with_nc:
        if cand.get("nc_pending"):
            return "pending", "the non-clairvoyant hand-offs are still running"
        if cand.get("nc_skipped"):
            return "unclear", ("no blind hint: the non-clairvoyant search does not finish on this deck "
                               "(built with --no-nc-deck)")
        if any(r.get("nc_error") for r in rungs):
            return "unclear", "no blind hint: the non-clairvoyant hand-off did not finish"
        return "unclear", "no non-clairvoyant ladder (built with --no-nc)"
    for x in with_nc:
        x["hint"] = "likely-misplay" if x["nc_after"] > x["nc_from"] else "likely-clairvoyance"
    gains = [x for x in with_nc if x["hint"] == "likely-misplay"]
    turns = ", ".join("T%d" % x["turn"] for x in lost)
    blind = ", ".join("T%d (blind search T%d, your line T%d)" % (x["turn"], x["nc_from"], x["nc_after"])
                      for x in gains)
    if gains and len(gains) == len(with_nc):
        return "likely-misplay", "a non-clairvoyant search also gains on " + blind
    if gains:
        return "mixed", ("lost ground on %s; a non-clairvoyant search also gains on %s, but not on the "
                         "rest" % (turns, blind))
    return "likely-clairvoyance", ("the search gains on %s, but a non-clairvoyant search does not"
                                   % turns)


class Pool:
    """ONE pool of worker threads over a PRIORITY queue, for the whole build: each reference's
    validation (0), then its own game + clairvoyant ladder (1), then its non-clairvoyant rungs (2).
    A task may enqueue more, so a game's next stage starts the moment its own previous stage is done
    -- there is no barrier between games, and the cheap work that makes a game reviewable always runs
    ahead of the expensive hint work."""

    def __init__(self, n):
        self.q, self.seq, self.live = queue.PriorityQueue(), 0, 0
        self.cv, self.err = threading.Condition(), []
        self.threads = [threading.Thread(target=self._run, daemon=True) for _ in range(n)]
        for t in self.threads:
            t.start()

    def put(self, prio, fn):
        with self.cv:
            self.seq += 1
            self.live += 1
            self.q.put((prio, self.seq, fn))

    def _run(self):
        while True:
            _, _, fn = self.q.get()
            try:
                fn()
            except Exception as e:                                # noqa: BLE001
                import traceback
                self.err.append(traceback.format_exc())
                print("[review] TASK FAILED: %s" % e, flush=True)
            with self.cv:
                self.live -= 1
                self.cv.notify_all()

    def wait(self):
        with self.cv:
            while self.live:
                self.cv.wait()


def build(args):
    t0 = time.time()
    cands, totals = find(args)
    print("\n[review] %d of %d reference games: the search wins earlier (%d tied, %d search slower or "
          "no win, %d hand mismatch)" % (len(cands), totals["games"], totals["tied"], totals["short"],
                                         totals["hand_bad"]), flush=True)
    shutil.rmtree(os.path.join(SCRATCH, "jobs"), ignore_errors=True)
    shutil.rmtree(os.path.join(ROOT, OUT), ignore_errors=True)
    os.makedirs(os.path.join(ROOT, OUT))
    meta = {"generated_at": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds"),
            "src": src_tree(), "max_turns": args.max_turns, "totals": totals, "nc": not args.no_nc,
            "complete": False, "games_total": len(cands)}
    laid, lock = {}, threading.Lock()
    stats = collections.Counter()

    def publish(cand):
        """(Re)write this game's folder and the index: a game is reviewable as soon as its own ladder
        is in, and its hint fills in when its non-clairvoyant rungs land."""
        with lock:
            e = layout(cand, load_verdicts()["verdicts"])
            laid[e["key"]] = e
            write_index(meta, laid)

    pool = Pool(args.jobs or os.cpu_count() or 4)
    no_nc_decks = {deck_registry.slug(d) for d in args.no_nc_deck or []}
    meta["no_nc_decks"] = sorted(no_nc_decks)

    def stage_validate(cand):
        c = {}
        ok, kind, detail = vpc.check_reference(cand["path"], collect=c)
        cand["replay"] = {"ok": ok, "kind": kind, "detail": detail}
        if not ok or "resolved" not in c:
            cand["starts"] = []
            publish(cand)
            return
        cand["starts"] = enumerate_starts(cand, c)
        cand["side"] = list(c["side"])
        print("[review] %-55s you %-6s search %-2s replay=%-9s turns=%d" % (
            ref_key(cand["path"]), fmt_win(cand["human_win"]), cand["search_win"], kind,
            len(cand["starts"])), flush=True)
        tag = os.path.join(SCRATCH, "jobs", ref_key(cand["path"])[:-5])
        todo = [("human", None, c["resolved"], os.path.join(tag, "human"))]
        todo += [("cv", f["turn"], f["prefix"], os.path.join(tag, "cv_T%d" % f["turn"])) for f in cand["starts"]]
        if any(a.startswith("--legacy-") for a in c["side"]):
            todo.append(("now", None, [], None))   # turn 1 under TODAY's rules: did a rule change move it?
        cand["left"] = len(todo)
        for kind_, turn, prefix, log_dir in todo:
            pool.put(1, lambda k=kind_, t=turn, p=prefix, d=log_dir: stage_cv(cand, c, k, t, p, d))

    def stage_cv(cand, c, kind, turn, prefix, log_dir):
        r = engine(dict(c, side=[]) if kind == "now" else c, prefix, log_dir)
        stats["cached" if r.get("cached") else "ran"] += 1
        with lock:
            if kind == "human":
                cand["human_run"] = r
            elif kind == "now":
                cand["t1_now"] = r
            else:
                cand.setdefault("ladder_raw", {}).setdefault(turn, {})["cv"] = r
            cand["left"] -= 1
            last = cand["left"] == 0
        if not last:
            return
        cand["nc_skipped"] = cand["deck_key"] in no_nc_decks
        rungs = [] if (args.no_nc or cand["nc_skipped"]) else lost_rungs(cand)
        by_turn = {f["turn"]: f for f in cand["starts"]}
        cand["nc_pending"] = len(rungs)
        publish(cand)
        for t in rungs:
            pool.put(2, lambda t=t: stage_nc(cand, c, t, by_turn[t]["prefix"]))

    def stage_nc(cand, c, turn, prefix):
        r = engine(c, prefix, None, ("MTG_NC_SEARCH=1",))
        stats["cached" if r.get("cached") else "ran"] += 1
        with lock:
            cand.setdefault("ladder_raw", {}).setdefault(turn, {})["nc"] = r
            cand["nc_pending"] -= 1
        publish(cand)

    for cand in cands:
        cand["no_nc"], cand["max_turns"] = args.no_nc, args.max_turns
        pool.put(0, lambda cand=cand: stage_validate(cand))

    def heartbeat():
        while True:
            time.sleep(120)
            with lock:
                n_laid = len(laid)
                n_hint = sum(1 for e in laid.values() if e["hint"] != "pending")
            print("[review] %.0fs: %d/%d games reviewable, %d with their hint; %d engine games run, %d cached"
                  % (time.time() - t0, n_laid, len(cands), n_hint, stats["ran"], stats["cached"]), flush=True)
    threading.Thread(target=heartbeat, daemon=True).start()
    pool.wait()

    meta.update(complete=True, wall_s=round(time.time() - t0))
    verdicts = load_verdicts()["verdicts"]
    index = write_index(meta, laid)
    write_index_md(meta, index, verdicts)
    print("\n[review] %d games laid out under %s/ (wall %.0fs; %d engine games run, %d from cache)"
          % (len(index), OUT, time.time() - t0, stats["ran"], stats["cached"]))
    print_table(index, verdicts)
    if pool.err:
        print("\n[review] %d task(s) FAILED:\n%s" % (len(pool.err), "\n".join(pool.err)))
        return 1
    return 0


def write_index(meta, laid):
    index = sorted(laid.values(), key=lambda e: (e["ref_dir"].lower(), e["seed"], e["gi"]))
    meta["games_laid_out"] = len(index)
    tmp = os.path.join(ROOT, OUT, "index.json.tmp")
    with open(tmp, "w") as fh:
        json.dump({"meta": meta, "games": index}, fh, indent=1)
    os.replace(tmp, os.path.join(ROOT, OUT, "index.json"))
    return index


def rules_note(cand):
    """The recording-rule flags the human's game replays under (rules that changed after it was
    recorded), and the search's turn-1 win under those rules vs today's. When the two differ, part of
    the gap between your line and the search is a RULES change, not play."""
    flags = [a for a in (cand.get("side") or []) if a.startswith("--legacy-")]
    now = (cand.get("t1_now") or {})
    then = next((r.get("cv_win") for r in cand.get("ladder") or [] if r["turn"] == 1), None)
    if not flags or not now:
        return None
    return {"legacy_flags": flags, "t1_recorded_rules": then, "t1_current_rules": now.get("win"),
            "changed": now.get("win") != then}


def layout(cand, verdicts):
    """Write one game's folder; return its index entry."""
    key = ref_key(cand["path"])
    rel_dir = key[:-5]                                       # <ref dir>/<game>
    gdir = os.path.join(ROOT, OUT, rel_dir)
    os.makedirs(gdir, exist_ok=True)
    shutil.copyfile(cand["path"], os.path.join(gdir, "reference.json"))   # a COPY, never a move
    files = {"reference": "reference.json"}
    digest = None
    if os.path.exists(cand["bench_trace"]):
        shutil.copyfile(cand["bench_trace"], os.path.join(gdir, "search.json"))
        files["search"] = "search.json"
        digest = line_digest(json.load(open(cand["bench_trace"])))
    hr = cand.get("human_run") or {}
    human_ok = None
    if hr.get("log"):
        shutil.copyfile(hr["log"], os.path.join(gdir, "human.json"))
        files["human"] = "human.json"
        human_ok = hr.get("win") == cand["human_win"]
    ladder = []
    for f in cand.get("starts", []):
        raw = (cand.get("ladder_raw") or {}).get(f["turn"], {})
        rung = {"turn": f["turn"], "phase": f["phase"], "board": f["board"], "picks": len(f["prefix"])}
        cv, nc = raw.get("cv"), raw.get("nc")
        if cv:
            rung.update(cv_win=cv.get("win"), cv_secs=cv["secs"], cv_cmd=cv["cmd"])
            if cv.get("error"):
                rung["cv_error"] = cv["error"]
            if cv.get("log"):
                name = "handoff_T%d.json" % f["turn"]
                shutil.copyfile(cv["log"], os.path.join(gdir, name))
                rung["cv_log"] = name
        if nc:
            rung.update(nc_win=nc.get("win"), nc_secs=nc["secs"], nc_cmd=nc["cmd"])
            if nc.get("error"):
                rung["nc_error"] = nc["error"]
        ladder.append(rung)
    cand["ladder"] = ladder
    h, why = hint(cand) if cand["replay"]["ok"] else ("unclear", "the reference does not replay: "
                                                     + str(cand["replay"]["detail"]))
    entry = {"key": key, "dir": rel_dir, "ref_dir": os.path.dirname(key), "deck_key": cand["deck_key"],
             "seed": cand["seed"], "gi": cand["gi"],
             "human_win": cand["human_win"], "search_win": cand["search_win"],
             "delta": ((cand["human_win"] if cand["human_win"] is not None else cand["max_turns"] + 1)
                       - cand["search_win"]),
             "files": files, "search_digest": digest, "replay": cand["replay"],
             "human_log_matches": human_ok, "human_log_win": hr.get("win"),
             "human_cmd": hr.get("cmd"), "ladder": ladder, "lost_turns": cand.get("lost_turns", []),
             "hint": h, "hint_detail": why, "rules": rules_note(cand)}
    with open(os.path.join(gdir, "review.json"), "w") as fh:
        json.dump(entry, fh, indent=1)
    return entry


# ---- reporting -----------------------------------------------------------------------------

def fmt_win(w):
    return "T%d" % w if isinstance(w, int) else "no win"


def print_table(index, verdicts):
    print("%-50s %7s %7s %3s  %-8s %-20s %s" % ("reference", "you", "search", "d", "lost on",
                                                 "hint", "verdict"))
    for e in index:
        v = verdicts.get(e["key"])
        st = verdict_state(e, v)
        vtxt = "-" if st == "unreviewed" else ("%s%s" % (v["verdict"], " (STALE)" if st == "stale" else ""))
        lost = ",".join("T%d" % x["turn"] for x in e["lost_turns"]) or "-"
        print("%-50s %7s %7s %3d  %-8s %-20s %s" % (e["key"], fmt_win(e["human_win"]),
                                                   fmt_win(e["search_win"]), e["delta"], lost,
                                                   e["hint"], vtxt))


def write_index_md(meta, index, verdicts):
    L = ["# Reference review -- games where the search beat your line", "",
         "Generated %s at src `%s`. Open them side by side with `./review.sh` "
         "(http://localhost:8090)." % (meta["generated_at"], meta["src"][:12]), "",
         "%(faster)d of %(games)d reference games: the search wins earlier "
         "(%(tied)d tied, %(short)d search slower or no win, %(hand_bad)d hand mismatch)." % meta["totals"],
         "", "| reference | you | search | lost ground on | hint | verdict |", "|---|---|---|---|---|---|"]
    for e in index:
        v = verdicts.get(e["key"])
        st = verdict_state(e, v)
        vtxt = "" if st == "unreviewed" else v["verdict"] + (" **(stale)**" if st == "stale" else "")
        lost = ", ".join("T%d" % x["turn"] for x in e["lost_turns"]) or "-"
        L.append("| [%s](%s/review.json) | %s | %s | %s | %s | %s |" % (
            e["key"], e["dir"], fmt_win(e["human_win"]), fmt_win(e["search_win"]), lost, e["hint"], vtxt))
    with open(os.path.join(ROOT, OUT, "INDEX.md"), "w") as fh:
        fh.write("\n".join(L) + "\n")


def load_index():
    try:
        with open(os.path.join(ROOT, OUT, "index.json")) as fh:
            return json.load(fh)
    except FileNotFoundError:
        sys.exit("no %s/index.json -- run `python3 scripts/ref_review.py build` first" % OUT)


def status(args):
    idx = load_index()
    verdicts = load_verdicts()["verdicts"]
    if args.verdict:
        idx["games"] = [e for e in idx["games"] if (verdicts.get(e["key"]) or {}).get("verdict") == args.verdict
                        or (args.verdict == "unreviewed" and e["key"] not in verdicts)]
    m = idx["meta"]
    print("built %s at src %s (current src %s)" % (m["generated_at"], m["src"][:12], src_tree()[:12]))
    print_table(idx["games"], verdicts)
    states = [verdict_state(e, verdicts.get(e["key"])) for e in idx["games"]]
    print("\n%d games: %d reviewed, %d stale, %d unreviewed" % (
        len(states), states.count("current"), states.count("stale"), states.count("unreviewed")))
    return 0


def mark(args):
    idx = {e["key"]: e for e in load_index()["games"]}
    if args.key not in idx:
        sys.exit("'%s' is not in the review index (keys look like 'burn/claude_s3_gi2.json')" % args.key)
    e = idx[args.key]
    v = load_verdicts()
    v["verdicts"][args.key] = stamp(e, args.verdict, args.note)
    save_verdicts(v)
    print("%s -> %s (saved to %s; commit + push it)" % (args.key, args.verdict, VERDICTS))
    return 0


def stamp(e, verdict, note, **extra):
    """The verdict record -- the SAME shape tools/replay/review_server.js writes."""
    return dict(extra, **{"verdict": verdict, "note": note or "", "human_win": e["human_win"],
            "search_win": e["search_win"], "search_digest": e["search_digest"],
            "lost_turns": [x["turn"] for x in e["lost_turns"]], "hint": e["hint"],
            "at": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds")})


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build", help="find, localise and lay out every game the search beats")
    b.add_argument("--deck", nargs="*", default=None, help="deck slugs (default: every reference deck)")
    b.add_argument("--max-turns", type=int, default=deck_registry.MAX_TURNS)
    b.add_argument("--threads", type=int, default=0, help="the pooled bench batch's workers (0 = auto)")
    b.add_argument("--jobs", type=int, default=0, help="concurrent hand-off games (0 = every core)")
    b.add_argument("--no-nc", action="store_true", help="skip the non-clairvoyant ladder (no hints)")
    b.add_argument("--no-nc-deck", nargs="*", default=None, metavar="DECK",
                   help="skip the non-clairvoyant hint on these decks (deck_registry slugs): the NC search "
                        "has no work ceiling in claude-play and does not finish on some combo decks -- "
                        "on 2026-10-24 single games of hinata2, accomplishedalchemist and "
                        "eldrazidisplacerflicker ran 3.5 h without finishing")
    st = sub.add_parser("status", help="print the review table with verdicts")
    st.add_argument("--verdict", default=None,
                    help="only games with this verdict (promoted, clairvoyance, bug, replay_manually, unreviewed)")
    m = sub.add_parser("mark", help="record a verdict without the viewer")
    m.add_argument("key", help="reference key, e.g. treasure_hunt/claude_s10_gi9.json")
    m.add_argument("verdict", choices=VERDICT_KINDS)
    m.add_argument("--note", default="")
    args = ap.parse_args()
    os.chdir(ROOT)
    return {"build": build, "status": status, "mark": mark}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())

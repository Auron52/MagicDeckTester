#!/usr/bin/env python3
"""THE MANA-CLEAN GATE: a deck leaves beta only after a clean mana analysis (USER 2026-11-02).

The USER: "a requirement for a deck to pass beyond beta is a mana analysis that passes on a clean set of
games. An example could be 5000 games with no issues found and if we find any we fix those and then need
to run on another 5000 games. Naturally, some decks are unlikely to have any mana issues and should just
run 5000 games."

So, per deck:
  * a RUN is 5000 games under the mana completeness oracle (scripts/mana_oracle.py,
    docs/design/mana-completeness-oracle.md) at the deck's shipped play settings, plus the same games
    with the oracle OFF to prove it did not move play (an oracle that moves play audits other games);
  * a run is CLEAN when it finds no ISSUE -- no flag of any class the oracle counts as an engine miss
    (mana_oracle.MISS) and no ORACLE-GAP -- and play is byte-identical with the oracle off;
  * every run takes a FRESH block of games: run k plays the games after run k-1's, so a fix is never
    re-checked on the games it was fitted to. After a run with issues: fix them, then run again.

The record is test/mana_clean.json (committed, like test/ref_bench.json); the play viewer reads it in
tools/play/server.js (tierFrom). A run is stamped with HEAD:src and the decklist's hash. A later src
commit makes the record STALE, which carries the last verdict forward (the ref-bench rule, USER
2026-08-29); a changed decklist voids it.

    python3 scripts/mana_clean.py status                 # every shipping deck: verdict, next game block
    python3 scripts/mana_clean.py run                    # every deck that is DUE (below), ONE pooled batch
    python3 scripts/mana_clean.py run niv decks/Snow     # these decks, due or not
    python3 scripts/mana_clean.py run --resume <name>    # finish an interrupted run (same src required)
    python3 scripts/mana_clean.py rescore                # re-derive verdicts after a change to the failing classes

DUE: never run; the decklist changed; or the last run had issues and src has changed since (a fix may
have landed). A deck whose last run had issues on the CURRENT src is not due: the same engine would find
the same defects, so fix first (or name the deck to run it anyway).

The run is ONE `mtg --batch` per arm over every deck, each deck split into chunks of games (jobs) so an
interrupted run resumes at chunk granularity; there is no barrier between decks. The oracle's stderr is
filtered as it streams (ISSUE lines kept whole, the context classes cut to one-line counters): at full
detail it is ~40 KB per game. Output: logs/mana_clean/<name>/report.md + run.json (host drive);
scratch (batch output, the filtered stderr) on build/scratch/mana_clean/<name>/.
Exit: 0 = every deck run is clean; 1 = a deck has issues; 2 = the run failed or play moved.
"""
import argparse
import collections
import datetime
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import deck_registry  # noqa: E402
import mana_oracle as mo  # noqa: E402
from engine_bin import engine_bin, pgo_ensure, pgo_fresh  # noqa: E402

STATE = os.path.join(ROOT, "test", "mana_clean.json")
GAMES = 5000                     # the gate's minimum per run (USER 2026-11-02: "An example could be 5000 games")
MAX_GAMES = 100000               # USER 2026-11-02: "if we can easily run 50000-100000 without it taking a long
                                 # time that would be even better" -- a run is sized to --minutes, in [GAMES, MAX]
MINUTES = 30                     # the wall a run of one deck is sized to (both arms, every thread)
CHUNK = 250                      # the largest batch job (games): the resume granularity
DETAIL_CAP = 5000                # whole ISSUE lines kept per deck and class (a painland deck can flag DOMINATED-
                                 # LIFE hundreds of times per 20 games); the rest are counters, so counts stay exact
CHUNK_MS = 120000                # ...and a job's target cost, so an expensive deck's tail stays short
SEED_BASE = 30000000             # game g of a deck's sequence is seed SEED_BASE + g, game index g
# DOMINATED-LIFE (the same options bought for more life) is DISCLOSED, not failed (USER 2026-11-03: "Life is
# not a huge worry here unless it loses lines. That said, in the future we may need to dig further into those
# cases (for 1v1 for example)"). A payment whose lost life DOES cost a later line shows up at that later payment
# as a lost-option class (ENUM-GAP, APPLY-DROP ...), which still fails. Its whole lines are kept (to the same
# cap) in the filtered stderr so the 1v1 dig has its samples: docs/design/mana-clean-gate.md.
# EXCEPT in a LIFE-BREAKPOINT deck (USER, same day: "Except maybe in a 'life matters' deck that needs to reach
# certain breakpoints"), where a life short can be the breakpoint missed: there DOMINATED-LIFE fails. A deck is one
# when a card on either board reads its controller's life total against a threshold (Serra Ascendant's "30 or more
# life", Righteous Valkyrie's "at least 7 life more than your starting life total"), read off cards.json's oracle
# text with the [bracket notes] cut, so the rule follows the list rather than a hand-kept deck set.
DISCLOSED = ["DOMINATED-LIFE"]
# UNMODELED (the oracle cannot drive the plan) and CAPPED (its payment search hit the node cap) FAIL too (USER
# 2026-11-03: "Even if this means an issue in our instrument it means something to fix"): a plan the oracle gave no
# verdict on is a plan nobody has shown clean. CAPPED-DECISION (a decision spent its priced work budget, USER 2026-11-06:
# no deck may get indefinitely stuck) fails for the same reason.
ISSUE = [c for c in mo.MISS if c not in DISCLOSED] + ["ORACLE-GAP", "UNMODELED", "CAPPED", "CAPPED-DECISION"]
LIFE_BREAKPOINT = re.compile(r"\bat least \d+ life\b|(?<!gained )\b\d+ or more life\b|\blife total is (?:less|greater) than \d+",
                             re.I)
# Cards whose life clause is NOT a goldfish breakpoint (USER 2026-11-04), so they do not make DOMINATED-LIFE fail.
NOT_A_BREAKPOINT = {
    "Elderscale Wurm": "USER: \"I wouldn't check against Elderscale Wurm. It doesn't mind you being injured.\"",
    "Ajani, Strength of the Pride": "USER: \"Ajani, Strength of the Pride's ability is only relevant in 1v1, not goldfish.\"",
    "Bilbo, Birthday Celebrant": "USER: \"Bilbo I would also drop from consideration for this just to save unnecessary "
                                 "work.\"",
}
# PER-DECK ALLOWANCES (USER 2026-11-04: "Sure, you can build it. I would rather minimize the exceptions, though, as
# using this too much could get in the way of actually finding issues"). `<deck dir>/<stem>.mana_clean.json`:
#   {"allow": [{"class": "CAPPED", "match": "<regex on the whole flag line>", "reason": "...",
#               "approved": "USER <date>: <quote>"}]}
# A flag of a failing class whose whole line matches an entry is recorded as ALLOWED (counted and disclosed, never
# failed). An entry needs the USER's approval; an agent never adds one. Applied when a run is scored (the whole line
# is needed), so a new entry takes effect on the next run, not on `rescore`.
ALLOWED = "ALLOWED"


def allowances(deck_path):
    """-> [(class, compiled regex)] from the deck's allowance file, [] when it has none."""
    path = Path(os.path.join(ROOT, deck_path))
    f = path.with_name(path.stem + ".mana_clean.json")
    if not f.exists():
        return []
    out = []
    for e in json.load(open(f)).get("allow", []):
        if not e.get("approved", "").startswith("USER"):
            sys.exit("mana_clean: %s: an allowance needs the USER's approval (\"approved\": \"USER ...\")" % f)
        out.append((e["class"], re.compile(e["match"])))
    return out


COUNTER = re.compile(r"^\[mana-oracle\] (?P<cls>[A-Z-]+) seed=\d+ job=(?P<job>\S+)")


_CARDS = None


def life_breakpoints(deck_path):
    """-> the cards in the decklist (main and side) that read their controller's life total against a threshold."""
    global _CARDS
    from analyze_deck import LoadDeckCounts            # the engine's own board split (DeckLoader.cpp)
    if _CARDS is None:
        raw = json.load(open(os.path.join(ROOT, "src", "cards", "data", "cards.json")))
        rows = raw if isinstance(raw, list) else raw.get("cards", list(raw.values()))
        _CARDS = {c.get("name"): (c.get("oracle_text") or "").split("[")[0] for c in rows if isinstance(c, dict)}
    path = Path(os.path.join(ROOT, deck_path))
    names = set(LoadDeckCounts(path, "main")) | set(LoadDeckCounts(path, "side"))
    return sorted(n for n in names if n not in NOT_A_BREAKPOINT and LIFE_BREAKPOINT.search(_CARDS.get(n, "")))


def issue_classes(deck_path):
    """The classes that fail this deck: ISSUE, plus DOMINATED-LIFE in a life-breakpoint deck."""
    return ISSUE + (DISCLOSED if life_breakpoints(deck_path) else [])


def git(*args):
    return subprocess.run(["git", "-C", ROOT] + list(args), capture_output=True, text=True).stdout.strip()


def now_iso():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def deck_sha(path):
    """The decklist's identity, line endings folded (a Windows checkout must hash the same)."""
    data = open(os.path.join(ROOT, path), "rb").read().replace(b"\r\n", b"\n")
    return hashlib.sha1(data).hexdigest()


def load_state():
    try:
        return json.load(open(STATE))
    except (OSError, ValueError):
        return {}


def save_state(st):
    st["_note"] = ("The mana-clean gate (scripts/mana_clean.py): one record per mana-oracle run per deck. A deck "
                   "leaves beta only when its latest run is clean on >= games_required games of its current list.")
    st["games_required"] = GAMES
    st["issue_classes"] = ISSUE
    st["disclosed_classes"] = DISCLOSED
    st.setdefault("decks", {})
    tmp = STATE + ".tmp"
    with open(tmp, "w") as fh:
        json.dump(st, fh, indent=1, sort_keys=True)
        fh.write("\n")
    os.replace(tmp, STATE)


def shipping(decks):
    """Every SHIPPING list with a profile (an archived variant is decks/<Deck>/<v1-...>)."""
    return [d for k, d in sorted(decks.items())
            if os.path.normpath(os.path.relpath(d.dir, ROOT)).count(os.sep) == 1 and os.path.exists(d.profile)]


def verdict(entry, sha, src_now):
    """-> (state, latest run or None). state: never | changed | clean | stale-clean | issues | invalid."""
    runs = (entry or {}).get("runs") or []
    if not runs:
        return "never", None
    last = runs[-1]
    if last.get("deck_sha") != sha:
        return "changed", last
    if last["verdict"] == "clean":
        if last["games"] < GAMES:
            return "short", last           # a clean run on fewer games than the gate asks for
        return ("clean" if last["src"] == src_now else "stale-clean"), last
    return last["verdict"], last


def due(entry, sha, src_now):
    state, last = verdict(entry, sha, src_now)
    if state in ("never", "changed", "short"):
        return True
    if state in ("issues", "invalid"):
        return last["src"] != src_now
    return False


def next_game(entry):
    return max([r["game_index"] + r["games"] for r in (entry or {}).get("runs") or []] or [0])


def cmd_status(args, decks):
    st = load_state().get("decks", {})
    src_now = git("rev-parse", "HEAD:src")
    chosen = pick(args.decks, decks) if args.decks else shipping(decks)
    print("%-22s %-12s %8s %8s %-14s %s" % ("deck", "verdict", "games", "issues", "src", "next games"))
    for d in chosen:
        e = st.get(d.key)
        state, last = verdict(e, deck_sha(os.path.relpath(d.deck_file, ROOT)), src_now)
        n = next_game(e)
        print("%-22s %-12s %8s %8s %-14s %d-%d%s" % (
            d.key, state, last["games"] if last else "-", last["issues"] if last else "-",
            last["src"][:12] if last else "-", n, n + GAMES - 1,
            "  DUE" if due(e, deck_sha(os.path.relpath(d.deck_file, ROOT)), src_now) else ""))
    return 0


def pick(names, decks):
    alias = mo.suite_aliases()
    out = []
    for name in names:
        key = deck_registry.slug(os.path.basename(os.path.normpath(name)))
        key = key if key in decks else alias.get(name, key)
        if key not in decks:
            sys.exit("mana_clean: no deck '%s'" % name)
        out.append(decks[key])
    return out


def run_arm(mtg, manifest, level, out_path, err_path, threads, allow=None):
    """One pooled batch. stdout (the per-job result lines) to `out_path`; stderr filtered into `err_path`:
    ISSUE and DISCLOSED flag lines whole (the first DETAIL_CAP per deck and class), every other flag cut to a one-line
    counter, everything else as is. The counters keep every count exact."""
    env = dict(os.environ, MTG_MANA_ORACLE=str(level))
    whole = collections.Counter()
    with open(out_path, "w") as so, open(err_path, "w") as se:
        p = subprocess.Popen([mtg, "--batch", manifest, "--threads", str(threads)], stdout=so,
                             stderr=subprocess.PIPE, env=env, cwd=ROOT, text=True, errors="replace", bufsize=1)
        for ln in p.stderr:
            m = COUNTER.match(ln)
            if m and m.group("cls") in ISSUE and any(c == m.group("cls") and rx.search(ln)
                                                      for c, rx in (allow or {}).get(m.group("job").split("__g")[0], [])):
                ln = ln.replace("[mana-oracle] %s " % m.group("cls"), "[mana-oracle] %s " % ALLOWED, 1)
                m = COUNTER.match(ln)
            if m and m.group("cls") != "DECISION":
                key = (m.group("job").split("__")[0], m.group("cls"))
                if m.group("cls") in ISSUE + DISCLOSED + [ALLOWED] and whole[key] < DETAIL_CAP:
                    whole[key] += 1
                    se.write(ln)
                else:
                    se.write("[mana-oracle] %s seed=0 job=%s\n" % (m.group("cls"), m.group("job")))
            else:
                se.write(ln)
        return p.wait()


def job_ms(out_path):
    """{job: (played, ms)} from a batch's per-job result lines."""
    out = {}
    for ln in open(out_path, errors="replace"):
        m = re.match(r"(\S+): played=(\d+) .* ms=(\d+)", ln)
        if m:
            out[m.group(1)] = (int(m.group(2)), int(m.group(3)))
    return out


def cost_per_game(d, g0, threads, scratch, prior):
    """(on, off) core-ms per game for deck `d`: its last record's measurement, else a probe of the block's
    first 2*threads games as one-game jobs on both arms (the run replays them; a probe only sizes)."""
    if prior and prior.get("ms_on") and prior.get("games"):
        return prior["ms_on"] / prior["games"], prior.get("ms_off", 0) / prior["games"]
    n = 2 * threads
    jobs = [{"name": "%s__p%d" % (d.key, g0 + i), "deck": os.path.relpath(d.deck_file, ROOT),
             "profile": os.path.relpath(d.profile, ROOT), "games": 1, "seed": SEED_BASE + g0 + i,
             "game_index": g0 + i} for i in range(n)]
    manifest = os.path.join(scratch, "probe.%s.json" % d.key)
    json.dump({"jobs": jobs}, open(manifest, "w"), indent=1)
    cost = []
    for arm, level in (("on", 2), ("off", 0)):
        o = os.path.join(scratch, "probe.%s.%s.out" % (d.key, arm))
        print("mana_clean: sizing %s: %d games, %s arm" % (d.key, n, arm), file=sys.stderr, flush=True)
        run_arm(engine_bin("mtg"), manifest, level, o, os.devnull, threads)
        r = job_ms(o).values()
        cost.append(sum(ms for _, ms in r) / max(1, sum(p for p, _ in r)))
    return cost[0], cost[1]


def size_run(on, off, threads, minutes):
    """-> (games, chunk). Games fill `minutes` of wall on `threads`, in [GAMES, MAX_GAMES], rounded down to
    1000; a chunk is at most CHUNK games and CHUNK_MS of work, and small enough for 8 jobs per thread."""
    per = max(on + off, 1e-3)
    games = int(minutes * 60000 * threads / per) // 1000 * 1000
    games = max(GAMES, min(MAX_GAMES, games))
    chunk = max(1, min(CHUNK, games // (8 * threads), int(CHUNK_MS / max(on, 1e-3))))
    return games, chunk


def done_jobs(path):
    return set(mo.digests(path)) if os.path.exists(path) else set()


def cmd_rescore(args, decks):
    """Re-derive each record's issue count and verdict from its exact per-class counts (`by_class`) under the
    CURRENT failing classes, for records scored under an older ISSUE set. A clean/issues verdict is all that
    moves; an invalid or incomplete run stays what it was (play identity is not a class count)."""
    st = load_state()
    for key, e in sorted(st.get("decks", {}).items()):
        for r in e.get("runs", []):
            fails = issue_classes(r["deck"])
            issues = sum(r["by_class"].get(k, 0) for k in fails)
            v = r["verdict"] if r["verdict"] not in ("clean", "issues") else ("clean" if issues == 0 else "issues")
            if (issues, v, fails) != (r["issues"], r["verdict"], r.get("issue_classes")):
                print("mana_clean: %-22s %s: %d issues %s -> %d %s" % (key, r["run"], r["issues"], r["verdict"],
                                                                     issues, v), file=sys.stderr)
                r.update(issues=issues, verdict=v, issue_classes=fails, life_breakpoints=life_breakpoints(r["deck"]))
    save_state(st)
    return 0


def cmd_run(args, decks):
    if git("status", "--porcelain", "--untracked-files=no", "--", "src"):
        sys.exit("mana_clean: src/ has uncommitted changes -- a gate run is stamped with HEAD:src, so commit first")
    src_now = git("rev-parse", "HEAD:src")
    st = load_state()
    st.setdefault("decks", {})

    if args.resume:
        out_dir = os.path.join(ROOT, "logs", "mana_clean", args.resume)
        plan = json.load(open(os.path.join(out_dir, "run.json")))
        if plan["src"] != src_now:
            sys.exit("mana_clean: run %s was planned at src %s, HEAD:src is %s -- start a new run"
                     % (args.resume, plan["src"][:12], src_now[:12]))
        name = args.resume
    else:
        chosen = pick(args.decks, decks) if args.decks else [
            d for d in shipping(decks)
            if due(st["decks"].get(d.key), deck_sha(os.path.relpath(d.deck_file, ROOT)), src_now)]
        if not chosen:
            print("mana_clean: no deck is due (status lists them)", file=sys.stderr)
            return 0
        name = args.name or datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%d-%H%M%S")
        out_dir = os.path.join(ROOT, "logs", "mana_clean", name)
        os.makedirs(out_dir, exist_ok=True)
    pgo_ensure()
    if not pgo_fresh():
        sys.exit("mana_clean: build/PGO is not built from HEAD:src (MTG_PGO=0, or its build failed) -- a gate "
                 "record must name the binary's src tree; the PGO stamp is the only proof of it")
    mtg = engine_bin("mtg")
    scratch = os.path.join(ROOT, "build", "scratch", "mana_clean", name)
    os.makedirs(scratch, exist_ok=True)

    if not args.resume:
        plan = {"name": name, "src": src_now, "commit": git("rev-parse", "HEAD"), "minutes": args.minutes,
                "started": now_iso(), "decks": []}
        for d in chosen:
            deck = os.path.relpath(d.deck_file, ROOT)
            e = st["decks"].get(d.key)
            g0 = next_game(e)
            last = (e or {}).get("runs", [None])[-1] if e and e.get("runs") else None
            on, off = ((0.0, 0.0) if args.games and args.chunk
                       else cost_per_game(d, g0, args.threads, scratch, last))
            games, chunk = size_run(on, off, args.threads, args.minutes)
            if args.games:
                games = args.games
            if args.chunk:
                chunk = args.chunk
            print("mana_clean: %s: %.0f + %.0f core-ms/game -> %d games in chunks of %d (games %d-%d)"
                  % (d.key, on, off, games, chunk, g0, g0 + games - 1), file=sys.stderr, flush=True)
            plan["decks"].append({"key": d.key, "deck": deck, "deck_sha": deck_sha(deck),
                                  "profile": os.path.relpath(d.profile, ROOT), "game_index": g0,
                                  "seed": SEED_BASE + g0, "games": games, "chunk": chunk,
                                  "est_ms_per_game": [round(on, 1), round(off, 1)]})
        json.dump(plan, open(os.path.join(out_dir, "run.json"), "w"), indent=1)

    jobs = []
    for pd in plan["decks"]:
        chunk = pd.get("chunk", plan.get("chunk", CHUNK))   # a resume must rebuild the SAME job names
        for off in range(0, pd["games"], chunk):
            gi = pd["game_index"] + off
            jobs.append({"name": "%s__g%d" % (pd["key"], gi), "deck": pd["deck"], "profile": pd["profile"],
                         "games": min(chunk, pd["games"] - off), "seed": SEED_BASE + gi, "game_index": gi})
    deck_of = {j["name"]: j for j in jobs}
    group = lambda job: job.split("__g")[0]  # noqa: E731

    # Each arm resumes at chunk granularity: attempt k runs the chunks no earlier attempt finished.
    rc = {}
    for arm, level in (("on", 2), ("off", 0)):
        k = 0
        while os.path.exists(os.path.join(scratch, "%s.%d.out" % (arm, k))):
            k += 1
        finished = set().union(*[done_jobs(os.path.join(scratch, "%s.%d.out" % (arm, i))) for i in range(k)])
        todo = [j for j in jobs if j["name"] not in finished]
        if not todo:
            continue
        manifest = os.path.join(scratch, "manifest.%s.%d.json" % (arm, k))
        json.dump({"jobs": todo}, open(manifest, "w"), indent=1)
        print("mana_clean: %s arm attempt %d: %d chunks / %d games over %d decks, %s"
              % (arm, k, len(todo), sum(j["games"] for j in todo), len({group(j["name"]) for j in todo}), mtg),
              file=sys.stderr, flush=True)
        rc[arm] = run_arm(mtg, manifest, level, os.path.join(scratch, "%s.%d.out" % (arm, k)),
                          os.path.join(scratch, "%s.%d.err" % (arm, k)), args.threads,
                          {pd["key"]: allowances(pd["deck"]) for pd in plan["decks"]})

    # Gather, counting each chunk only from the attempt that finished it (an interrupted attempt's
    # partial lines for a chunk that a later attempt re-ran must not count twice).
    def attempts(arm):
        out, k = [], 0
        while os.path.exists(os.path.join(scratch, "%s.%d.out" % (arm, k))):
            out.append((os.path.join(scratch, "%s.%d.out" % (arm, k)), os.path.join(scratch, "%s.%d.err" % (arm, k))))
            k += 1
        return out

    dig = {"on": {}, "off": {}}
    ms = {"on": {}, "off": {}}
    for arm in dig:
        for o, _ in attempts(arm):
            for job, v in mo.digests(o).items():
                dig[arm].setdefault(job, v)
            for job, v in job_ms(o).items():
                ms[arm].setdefault(job, v[1])
    owner = {}
    for i, (o, _) in enumerate(attempts("on")):
        for job in mo.digests(o):
            owner.setdefault(job, i)

    counts = collections.defaultdict(collections.Counter)
    fails = collections.defaultdict(lambda: ISSUE, {pd["key"]: issue_classes(pd["deck"]) for pd in plan["decks"]})
    decisions, plans_seen, summary, flags = collections.Counter(), collections.Counter(), [], []
    for i, (_, e) in enumerate(attempts("on")):
        for ln in open(e, errors="replace"):
            ln = ln.rstrip("\n")
            if ln.startswith("=== MANA ORACLE"):
                summary.append(ln)
                continue
            m = mo.DECISION.match(ln)
            if m:
                if owner.get(m.group("job")) == i:
                    decisions[group(m.group("job"))] += 1
                    plans_seen[group(m.group("job"))] += int(m.group("n"))
                continue
            m = COUNTER.match(ln)
            if not m or owner.get(m.group("job")) != i:
                continue
            counts[group(m.group("job"))][m.group("cls")] += 1
            if m.group("cls") in fails[group(m.group("job"))]:
                rec = mo.flag_record(ln, deck_of, group)
                if rec:
                    flags.append(rec)

    when = now_iso()
    st = load_state()                      # re-read: a run takes hours and another run or a rescore may have
    st.setdefault("decks", {})             # written since this one loaded it; only this run's records change
    rows, worst = [], 0
    for pd in plan["decks"]:
        mine = [j["name"] for j in jobs if group(j["name"]) == pd["key"]]
        complete = all(j in dig["on"] and j in dig["off"] for j in mine)
        identical = complete and all(dig["on"][j] == dig["off"][j] for j in mine)
        played = sum(int(dig["on"][j][0]) for j in mine if j in dig["on"])
        c = counts[pd["key"]]
        issues = sum(c[k] for k in fails[pd["key"]])
        if not complete or played < pd["games"]:
            v = "incomplete"
        elif not identical:
            v = "invalid"
        else:
            v = "clean" if issues == 0 else "issues"
        worst = max(worst, {"clean": 0, "issues": 1}.get(v, 2))
        rows.append((pd["key"], v, played, issues, c))
        if v == "incomplete":
            continue                       # nothing recorded: --resume finishes it
        rec = {"run": name, "date": when, "src": plan["src"], "commit": plan["commit"], "binary": "PGO",
               "deck": pd["deck"], "deck_sha": pd["deck_sha"], "game_index": pd["game_index"], "seed": pd["seed"],
               "games": played, "issues": issues, "identity": identical, "verdict": v,
               "issue_classes": fails[pd["key"]], "life_breakpoints": life_breakpoints(pd["deck"]),
               "by_class": {k: c[k] for k in sorted(c)}, "decisions": decisions[pd["key"]],
               "ms_on": sum(ms["on"].get(j, 0) for j in mine), "ms_off": sum(ms["off"].get(j, 0) for j in mine),
               "report": os.path.relpath(os.path.join(out_dir, "report.md"), ROOT)}
        e = st["decks"].setdefault(pd["key"], {"runs": []})
        e["runs"] = [r for r in e["runs"] if r.get("run") != name] + [rec]
    save_state(st)

    # The report: a verdict table, the per-class counts (every class -- the context ones are disclosure),
    # then mana_oracle's own sections over the ISSUE flags, with their repro commands.
    cls_cols = [k for k in mo.ORDER if any(r[4][k] for r in rows)]
    head = ["## Verdict per deck\n",
            "| deck | games | verdict | issues | " + " | ".join(cls_cols) + " |",
            "|---|---|---|---|" + "---|" * len(cls_cols)]
    for key, v, played, issues, c in rows:
        head.append("| %s | %d | %s | %d | %s |" % (key, played, v, issues, " | ".join(str(c[k]) for k in cls_cols)))
    blocks = ", ".join("%s %d-%d" % (pd["key"], pd["game_index"], pd["game_index"] + pd["games"] - 1)
                       for pd in plan["decks"])
    header = ("Mana-clean gate run: %d decks, games (seed %d + game index) %s; src `%s`, binary `%s`, "
              "`MTG_MANA_ORACLE=2`. ISSUE classes: %s (disclosed, not failed: %s). The sections below the verdict table read only the first "
              "%d whole flag lines per deck and class; the table's counts are exact.\n\n%s"
              % (len(plan["decks"]), SEED_BASE, blocks, plan["src"][:12], os.path.relpath(mtg, ROOT),
                 ", ".join(ISSUE), ", ".join(DISCLOSED), DETAIL_CAP, "\n".join(head)))
    identity_all = all(r[1] != "invalid" for r in rows)
    text, _, _, _ = mo.report("Mana-clean gate `%s`" % name, header, [pd["key"] for pd in plan["decks"]], deck_of,
                              flags, decisions, plans_seen, summary, identity_all, rc.get("on", 0), args.top)
    open(os.path.join(out_dir, "report.md"), "w").write(text)
    for key, v, played, issues, _ in rows:
        print("mana_clean: %-22s %-10s %5d games, %d issues" % (key, v, played, issues), file=sys.stderr)
    print("mana_clean: -> %s; record %s" % (os.path.relpath(os.path.join(out_dir, "report.md"), ROOT),
                                            os.path.relpath(STATE, ROOT)), file=sys.stderr)
    return worst


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("status")
    s.add_argument("decks", nargs="*")
    r = sub.add_parser("run")
    r.add_argument("decks", nargs="*", help="deck keys / folders (default: every DUE shipping deck)")
    r.add_argument("--games", type=int, default=None,
                   help="games per deck (default: sized to --minutes from the deck's measured cost, %d-%d)"
                   % (GAMES, MAX_GAMES))
    r.add_argument("--minutes", type=float, default=MINUTES, help="the wall one deck's run is sized to")
    r.add_argument("--threads", type=int, default=os.cpu_count() or 8)
    r.add_argument("--name", default=None)
    r.add_argument("--resume", default=None, help="finish an interrupted run of this name")
    r.add_argument("--top", type=int, default=12)
    r.add_argument("--chunk", type=int, default=None, help="games per batch job (default: sized per deck)")
    q = sub.add_parser("rescore", help="re-derive issue counts and verdicts from by_class under the current "
                       "failing classes (after a change to ISSUE)")
    for p in (s, r, q):
        p.add_argument("--state", default=None, help="the record file (default test/mana_clean.json; a test run "
                       "points it elsewhere so it does not spend a deck's game blocks)")
    args = ap.parse_args()
    if args.state:
        global STATE
        STATE = os.path.abspath(args.state)
    decks = deck_registry.discover(os.path.join(ROOT, "decks"))
    return {"status": cmd_status, "rescore": cmd_rescore}.get(args.cmd, cmd_run)(args, decks)


if __name__ == "__main__":
    sys.exit(main())

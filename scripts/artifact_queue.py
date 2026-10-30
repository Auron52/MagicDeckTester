#!/usr/bin/env python3
"""The ARTIFACT REFRESH QUEUE: rank every deck's generated artifacts by how far the play they were fitted
to has drifted, and let one or more idle machines regenerate them, most-drifted first.

    python3 scripts/artifact_queue.py status [--json] [--all]   # the ranked list; reads git only, plays nothing
    python3 scripts/artifact_queue.py explain <deck>             # every axis for one deck
    python3 scripts/artifact_queue.py mark <deck> <mulligan|valueleaf> "<why>" [--priority N]
    python3 scripts/artifact_queue.py unmark <deck> <mulligan|valueleaf>
    python3 scripts/artifact_queue.py claims                     # what every machine is doing (remote)
    python3 scripts/artifact_queue.py start                      # detach a worker loop (own session, nohup)
    python3 scripts/artifact_queue.py run [--once] [--dry-run]   # the worker loop, in the foreground
    python3 scripts/artifact_queue.py stop                       # finish the CURRENT job, then exit
    python3 scripts/artifact_queue.py selftest                   # claim CAS, .gz round-trip, parsing (no games)

Design + rationale: docs/design/artifact-staleness-queue.md (USER idea, 2026-09-03 / 2026-10-10;
built 2026-10-23 when inference budget ran short and the CPU was the free resource).

WHAT "STALE" MEANS HERE -- every axis is computed from git and the files themselves, no games played:

  * PLAY DRIFT (the ranking). The regression harness keeps, per suite case, one
    "<game_index> <win_turn> <decision-stream digest>" line per game (test/gt_logs/*.wins). For each of
    the deck's cases we take the version first accepted AT OR AFTER the artifact's adoption commit (the
    first GT that saw the artifact live) and diff it, game by game, with HEAD's. churn = the share of
    the deck's suite games whose decision stream changed since; |dturn| = mean per-game win-turn
    change. Taking the first accept AFTER adoption means the artifact's own effect is never counted as
    drift; the price is that a play change landed between adoption and that accept is missed (an
    UNDER-estimate, never an over-one). The SCORE is |dturn|; churn is shown but saturates (see score()).
  * DECKLIST MISMATCH (exact, keep tables): a card the list plays that no bucket names, or a bucketed
    card the list no longer plays. Every hand holding one misses the table.
  * CARD DATA (exact): a deck card's cards.json entry (minus name/oracle_text, the same canonicalisation
    as CardDefHash) changed since the artifact was adopted.
  * SETTINGS (exact, keep tables): the table was generated at a depth/budget other than the deck's
    current mull_gen_depth / mull_gen_budget_ms.
  * PIPELINE ORDER (exact, value leaves): the keep table was re-adopted after the leaf was fitted, so the
    leaf was fitted to the hands an older table kept (USER order 2026-10-18: profile -> mulligan -> leaf).
  * MANUAL MARK (USER): scripts/artifact_queue.json "marks" -- puts an artifact at the front regardless
    of measured drift, with a reason (the 2026-10-10 Snow keep table is the motivating case), until a run
    started after the mark records a verdict for it in the deck's ledger (mark_answer).

THE WORKER. A machine that runs `run` claims ONE DECK at a time from the shared queue branch
(`artifact-queue` on origin; a claim is a compare-and-swap push, so two machines can never claim the same
deck), freezes a dedicated worktree (.claude/worktrees/artifact-worker-<id>) on origin's branch tip, and
runs that deck's stages STRICTLY SERIALLY through the only sanctioned entry points -- `mullgen.sh run`
then `valueleaf.sh run` (CLAUDE.md: generation stages are serial, in order, alone on the box). It waits
while any foreign `mtg`/`mtg-analyze` process is on the box (NOTHING runs alongside a generation), never
kills anything, renews its lease every 20 min, and when the deck is done commits whatever the drivers
adopted onto a result branch `artifact-refresh/<deck>-<date>` and pushes it. LANDING that branch (and
accepting smoke/regression GT for the play it moves) stays a human/agent step: the worker never touches
the working branch.
"""
import argparse
import datetime
import gzip
import glob
import hashlib
import json
import math
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import threading
import time
import xml.etree.ElementTree as ET

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import deck_registry   # noqa: E402

CONFIG_REL = "scripts/artifact_queue.json"
GT_LOGS = "test/gt_logs"
CARDS_JSON = "src/cards/data/cards.json"
QUEUE_BRANCH = "artifact-queue"
RESULT_PREFIX = "artifact-refresh/"
KINDS = ("mulligan", "valueleaf")
WINS_RE = re.compile(r"^(?P<key>[a-z0-9_]+?)(?P<hg>2hg)?_(?P<mode>smoke|regression|overnight)_d(?P<d>\d+)_s(?P<s>\d+)\.wins$")

DEFAULT_CONFIG = {
    "branch": "phase-1-2-deck-analyzer",
    "min_drift": 0.02,
    "kind_weight": {"mulligan": 1.0, "valueleaf": 1.0},
    "recipe_overrides": {},
    "mulligan_env": {},
    "leaf_follows_mulligan": False,
    "exclude": [],
    "lease_hours": 3,
    "heartbeat_minutes": 20,
    "idle_poll_minutes": 5,
    "idle_max_load": None,
    "retry_rejected_days": 14,                # FAILED claims only (a crash is no verdict on the deck)
    "auto_land": True,
    "marks": [],
}


# ------------------------------------------------------------------------------------------------ git
def git(*args, cwd=ROOT, check=True, inp=None):
    r = subprocess.run(["git", *args], cwd=cwd, input=inp, capture_output=True, text=True)
    if check and r.returncode != 0:
        raise RuntimeError("git %s failed: %s" % (" ".join(args), r.stderr.strip()))
    return r.stdout


class Blobs(object):
    """One long-lived `git cat-file --batch`: reading ~1000 historical files through one pipe instead of
    one process per file is what makes `status` a few seconds rather than minutes on the 9p mount."""

    def __init__(self, cwd=ROOT):
        self.p = subprocess.Popen(["git", "cat-file", "--batch"], cwd=cwd, stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE)
        self.cache = {}

    def get(self, rev, path):
        spec = "%s:%s" % (rev, path)
        if spec in self.cache:
            return self.cache[spec]
        self.p.stdin.write((spec + "\n").encode())
        self.p.stdin.flush()
        head = self.p.stdout.readline().decode().split()
        if len(head) < 3 or head[1] != "blob":
            self.cache[spec] = None
            return None
        n = int(head[2])
        data = self.p.stdout.read(n)
        self.p.stdout.read(1)
        self.cache[spec] = data
        return data

    def close(self):
        try:
            self.p.stdin.close()
            self.p.wait(timeout=10)
        except Exception:
            self.p.kill()


# --------------------------------------------------------------------------------------------- config
def load_config(root=ROOT):
    cfg = json.loads(json.dumps(DEFAULT_CONFIG))
    p = os.path.join(root, CONFIG_REL)
    if os.path.exists(p):
        user = json.load(open(p))
        for k, v in user.items():
            if k.startswith("_"):
                continue
            if isinstance(v, dict) and isinstance(cfg.get(k), dict):
                cfg[k].update(v)
            else:
                cfg[k] = v
    return cfg


def save_marks(marks, root=ROOT):
    p = os.path.join(root, CONFIG_REL)
    raw = json.load(open(p)) if os.path.exists(p) else {}
    raw["marks"] = marks
    with open(p, "w") as f:
        json.dump(raw, f, indent=2)
        f.write("\n")


# -------------------------------------------------------------------------------------------- decks
def main_cards(path, data=None):
    """-> set of mainboard card names of a .cod (XML) or .txt ("N Name") list."""
    if data is None:
        data = open(path, "rb").read()
    names = set()
    if path.lower().endswith(".cod"):
        for z in ET.fromstring(data).iter("zone"):
            if z.get("name") == "main":
                for c in z.iter("card"):
                    names.add(c.get("name"))
        return names
    for line in data.decode("utf-8", "replace").splitlines():
        s = line.strip()
        if not s or s.startswith("#") or s.startswith("//"):
            continue
        if s.lower().startswith("sideboard") or s.upper().startswith("SB:"):
            break
        m = re.match(r"^(\d+)x?\s+(.+)$", s)
        if m:
            names.add(m.group(2).strip())
    return names


def suite_map(root=ROOT):
    """-> {decklist relpath: suite key}, parsed from test/regression_cases.sh (see suite_gate._read_map)."""
    txt = open(os.path.join(root, "test/regression_cases.sh")).read()
    m = re.search(r"declare -A DECK_FILE=\((.*?)\n\)", txt, re.S)
    out = {}
    if m:
        for k, v in re.findall(r"\[([A-Za-z0-9_]+)\]=(\"[^\"]*\"|\S+)", m.group(1)):
            out[v.strip('"')] = k
    return out


def shipping_decks(root=ROOT):
    """The bare-key decks (the list that ships), never an archived v<N>- folder or a candidate variant."""
    cwd = os.getcwd()
    os.chdir(root)
    try:
        decks = deck_registry.discover()
    finally:
        os.chdir(cwd)
    out = []
    for key, d in sorted(decks.items()):
        if os.path.normpath(d.dir).count(os.sep) != 1:      # decks/<Name> only
            continue
        out.append(d)
    return out


def read_json_any(path_or_bytes, gz=False):
    if isinstance(path_or_bytes, (bytes, bytearray)):
        b = gzip.decompress(path_or_bytes) if gz else path_or_bytes
        return json.loads(b.decode("utf-8"))
    if path_or_bytes.endswith(".gz"):
        return json.load(gzip.open(path_or_bytes, "rt"))
    return json.load(open(path_or_bytes))


def keep_paths(d, R=ROOT):
    base = "%s/%s.keepmodel.exhaustive" % (d.dir, d.stem)
    for prof in (base + ".profile.json.gz", base + ".profile.json"):
        if os.path.exists(os.path.join(R, prof)):
            raw = base + ".raw.json.gz"
            if not os.path.exists(os.path.join(R, raw)):
                raw = base + ".raw.json"
            return prof, raw
    return None, None


def eval_model_hash(value_json):
    em = value_json.get("eval_model")
    if not em:
        return None
    return hashlib.sha256(json.dumps(em, sort_keys=True).encode()).hexdigest()


def canon_card(entry):
    e = dict(entry)
    e.pop("name", None)
    e.pop("oracle_text", None)
    return json.dumps(e, sort_keys=True, separators=(",", ":"))


# ------------------------------------------------------------------------------------------ history
class History(object):
    """Linear (first-parent) history, positions oldest=0, and per-path commit lists -- built from a FEW
    whole-history walks up front, because one `git log -- <path>` per artifact cost seconds each on the
    9p mount. CLAUDE.md forbids merge commits, so first-parent order IS the history."""

    def __init__(self, rev="HEAD", root=ROOT):
        self.root = root
        self.rev = rev
        self.order, self.dates = [], {}
        for line in git("log", "--first-parent", "--reverse", "--format=%H %cs", rev, cwd=root).splitlines():
            h, dt = line.split()
            self.order.append(h)
            self.dates[h] = dt
        self.pos = {h: i for i, h in enumerate(self.order)}
        self.blobs = Blobs(root)
        self.paths = {}                       # path -> [positions ascending]
        cur = None
        out = git("-c", "core.quotePath=false", "log", "--first-parent", "--format=C %H", "--name-only", rev, "--",
                  "decks", GT_LOGS, CARDS_JSON, cwd=root)
        for line in out.splitlines():
            if line.startswith("C "):
                cur = self.pos.get(line[2:].strip())
            elif line.strip() and cur is not None:
                self.paths.setdefault(line.strip(), []).append(cur)
        for v in self.paths.values():
            v.sort()
        self.src_pos = sorted(self.pos[h] for h in git("log", "--first-parent", "--format=%H", rev, "--", "src",
                                                       cwd=root).split() if h in self.pos)
        self.tree = {}
        for line in git("-c", "core.quotePath=false", "ls-tree", "-r", rev, "decks", cwd=root).splitlines():
            meta, path = line.split("\t", 1)
            self.tree[path] = meta.split()[2]
        self.dirty = set()
        for line in git("-c", "core.quotePath=false", "status", "--porcelain", "--", "decks", cwd=root).splitlines():
            self.dirty.add(line[3:].strip().strip('"'))
        self._cards = {}

    def resolve(self, h):
        """A commit not on the first-parent line (rare) maps to its nearest first-parent descendant."""
        if h in self.pos:
            return h
        out = git("rev-list", "--first-parent", "--ancestry-path", "%s..%s" % (h, self.rev), cwd=self.root,
                  check=False).split()
        return out[-1] if out else self.order[-1]

    def last_touch(self, path):
        p = self.paths.get(path)
        return self.order[p[-1]] if p else None

    def touches(self, path):
        """-> commits (newest first) that changed `path`."""
        return [self.order[i] for i in reversed(self.paths.get(path, []))]

    def src_commits_since(self, h):
        import bisect
        return len(self.src_pos) - bisect.bisect_right(self.src_pos, self.pos[self.resolve(h)])

    def is_dirty(self, path):
        return path in self.dirty or any(x.endswith("/") and path.startswith(x) for x in self.dirty)

    def wins_index(self):
        """-> {wins filename: [positions ascending]}."""
        return {os.path.basename(p): v for p, v in self.paths.items() if p.startswith(GT_LOGS + "/")}

    def cards_at(self, rev):
        """cards.json as of `rev`, keyed by the cards.json commit in force there (so ~70 adoption commits
        cost a handful of parses of a 1 MB file, not 70)."""
        import bisect
        cp = self.paths.get(CARDS_JSON, [])
        i = bisect.bisect_right(cp, self.pos[self.resolve(rev)]) - 1
        key = self.order[cp[i]] if i >= 0 else rev
        if key not in self._cards:
            b = self.blobs.get(key, CARDS_JSON)
            m = {}
            if b:
                for e in json.loads(b.decode("utf-8")).get("cards", []):
                    if "name" in e:
                        m[e["name"]] = canon_card(e)
            self._cards[key] = m
        return self._cards[key]

    def short(self, h):
        return h[:9] if h else "-"

    def date(self, h):
        return self.dates.get(self.resolve(h), "-") if h else "-"


def parse_wins(data):
    out = {}
    if not data:
        return out
    for line in data.decode().splitlines():
        p = line.split()
        if len(p) >= 2:
            out[int(p[0])] = (int(p[1]), p[2] if len(p) > 2 else "")
    return out


def play_churn(H, suite_key, adopt_pos):
    """-> dict(games, changed, abs_dturn, net_dturn, cases, cases_with_baseline) over every suite case of
    the deck (standard and 2HG). See the module docstring for the baseline rule."""
    idx = H.wins_index()
    games = changed = 0
    abs_d = net_d = 0.0
    cases = with_base = 0
    for fname, poss in idx.items():
        m = WINS_RE.match(fname)
        if not m or m.group("key") != suite_key:
            continue
        cur = H.blobs.get(H.rev, "%s/%s" % (GT_LOGS, fname))
        if cur is None:
            continue                              # case retired
        cases += 1
        after = [p for p in poss if p >= adopt_pos]
        if not after:
            continue                              # no accept since adoption: nothing measured to move
        base = H.blobs.get(H.order[after[0]], "%s/%s" % (GT_LOGS, fname))
        b, c = parse_wins(base), parse_wins(cur)
        common = set(b) & set(c)
        if not common:
            continue
        with_base += 1
        for gi in common:
            games += 1
            if b[gi][1] != c[gi][1]:
                changed += 1
            dt = c[gi][0] - b[gi][0]
            abs_d += abs(dt)
            net_d += dt
    return {"games": games, "changed": changed, "cases": cases, "cases_measured": with_base,
            "churn": (changed / games) if games else 0.0,
            "abs_dturn": (abs_d / games) if games else 0.0,
            "net_dturn": (net_d / games) if games else 0.0}


# ----------------------------------------------------------------------------------------- staleness
def card_data_changes(H, adopt, cards):
    then, now = H.cards_at(adopt), H.cards_at(H.rev)
    return sorted(c for c in cards if then.get(c) != now.get(c) and (c in then or c in now))


# ------------------------------------------------------------------------------------------- ledger
# A regeneration that did NOT replace the incumbent is still a result: at the frozen commit, a fresh
# artifact at no-lower settings did not beat the shipped one. USER 2026-10-23: "if a new table or
# value-leaf doesn't improve quality we still don't want to do it again". So every verdict lands as an
# entry in test/artifact_ledger/<deck>.json (one file per deck, so result branches never conflict), and
# the drift of an artifact is measured from the LATER of its adoption and its last verification -- a
# kept incumbent only re-enters the queue once play has drifted again since the attempt.
LEDGER_DIR = "test/artifact_ledger"
VERIFYING = ("kept-incumbent", "adopted", "adopted-ungated")


def ledger_path(root, key):
    return os.path.join(root, LEDGER_DIR, key + ".json")


def read_ledger(root, key):
    try:
        return json.load(open(ledger_path(root, key))).get("entries", [])
    except Exception:
        return []


def append_ledger(root, key, entry):
    ents = read_ledger(root, key) + [entry]
    os.makedirs(os.path.join(root, LEDGER_DIR), exist_ok=True)
    with open(ledger_path(root, key), "w") as f:
        json.dump({"deck": key, "_doc": "artifact refresh verdicts -- scripts/artifact_queue.py (LEDGER_DIR)",
                   "entries": ents}, f, indent=1)
        f.write("\n")


# A USER mark is a REQUEST to regenerate, and a finished run ANSWERS it: any verdict the deck's ledger records for
# the marked kind from a run that STARTED after the mark was set (adopted, kept-incumbent, or a trade held for the
# USER). Before this a mark outlived its answer and re-queued the same artifact on every pick (2026-10-28: Angels
# regenerated again right after its marked table landed; Giants, answered 2026-10-25, started a third run).
MARK_ANSWERS = VERIFYING + ("trade",)
RUN_ID_TIME = re.compile(r"(\d{8}T\d{6})$")


def mark_time(m):
    """When the USER set the mark. `mark` records it to the second; an older date-only mark counts from the END of
    its day, so a run earlier that same day (possibly frozen before the mark's reason landed) never answers it."""
    if m.get("set"):
        return parse_iso(m["set"])
    if not m.get("date"):
        return None
    return datetime.datetime.strptime(m["date"], "%Y-%m-%d").replace(tzinfo=datetime.timezone.utc) \
        + datetime.timedelta(days=1)


def run_started(e):
    """When a ledger entry's run first claimed its deck: the stamp run_job puts on run_id, kept across a resume."""
    mt = RUN_ID_TIME.search(e.get("run_id") or "")
    return datetime.datetime.strptime(mt.group(1), "%Y%m%dT%H%M%S").replace(tzinfo=datetime.timezone.utc) \
        if mt else None


def mark_answer(m, kind, ledger):
    """-> the ledger entry that answers USER mark `m` on `kind`, or None while the mark is still owed."""
    t = mark_time(m)
    if t is None:
        return None
    for e in ledger:
        s = run_started(e)
        if e.get("kind") == kind and e.get("verdict") in MARK_ANSWERS and s is not None and s >= t:
            return e
    return None


def artifact_ident(kind, root, path):
    """The identity an artifact is verified/claimed under. A keep table: its file's git blob. A leaf: its
    eval_model's hash, NOT the sidecar's blob -- value.json also carries value_play settings, and an edit
    to those must not make a verified leaf look new."""
    p = os.path.join(root, path)
    if not os.path.exists(p):
        return None
    if kind == "valueleaf":
        h = eval_model_hash(json.load(open(p)))
        return ("em:" + h[:20]) if h else None
    return git("hash-object", p, cwd=root).strip()


def mull_plan(cfg, deck, inc_R, inc_depth, inc_budget, screened):
    """The mulligan regeneration's recipe: the incumbent's, chosen from `fast` and `complete` only (USER
    2026-10-25: "I'm okay with just considering complete and fast for this pipeline"; 2026-10-23: a much
    lower setting is "all but guaranteed to fail" the old-vs-new A/B).
      * R<=30 -> `fast` (R30, adaptive bottoming); R<=40, or not recorded -> `complete` (R40, full bottoming).
      * R>40 (burn/slivers/knights-era R=60 tables) is beyond `complete`: the plan is marked `beyond`, and
        such a table is LEFT AS IT IS (not queued) unless the USER marks it, when it runs `complete`.
      * depth / budget: the deck's screened mull_gen_* as-is (mullgen.sh's own choice). An incumbent's
        generation depth is reported (`explain`), never imposed.
    A per-deck recipe_overrides entry still wins (a USER decision)."""
    plan = {"env": {}, "depth": None, "budget": None, "why": [], "beyond": False}
    R = max([r for r in (inc_R,) if isinstance(r, int)] or [0])
    if deck in cfg.get("mulligan_env", {}):
        # A USER decision recorded in the config, e.g. {"MTG_KEEP_REDISCOVER": "1"}: re-bucket from scratch instead
        # of re-using the incumbent's buckets (USER 2026-10-30: Creature Giving K 21 -> 20, Overgrown Tomb merged).
        plan["env"].update({k: str(v) for k, v in cfg["mulligan_env"][deck].items()})
        plan["why"].append("env %s from mulligan_env" % ",".join("%s=%s" % kv for kv in sorted(plan["env"].items())))
    if deck in cfg.get("recipe_overrides", {}):
        plan["recipe"] = cfg["recipe_overrides"][deck]
        plan["why"].append("recipe %s from recipe_overrides" % plan["recipe"])
    elif R and R <= 30:
        plan["recipe"] = "fast"
    else:
        plan["recipe"] = "complete"
        if R > 40:
            plan["beyond"] = True
            plan["why"].append("incumbent R=%d is beyond complete: left as is unless USER-marked" % R)
    if R:
        plan["why"].insert(0, "incumbent R=%d -> %s" % (R, plan["recipe"]))
    return plan


def screened_setting(root, d):
    """-> ((depth, budget), file holding them) -- value.json first, then the profile (mullgen.sh's order)."""
    for src in (d["value"], d["profile"]):
        p = os.path.join(root, src)
        if os.path.exists(p):
            vp = json.load(open(p)).get("value_play", {}) or {}
            if "mull_gen_depth" in vp:
                return (vp.get("mull_gen_depth"), vp.get("mull_gen_budget_ms")), src
    return (None, None), None


def settings_answered(verified, src, setting, root):
    """Did the verifying verdict's run regenerate at `setting` (the deck's screened mull_gen depth/budget)?
    Exact: the screened setting is read from `src` as it stood at the verdict's frozen commit, so a later
    re-screen to a different setting raises the flag again."""
    if not verified or not src or not verified.get("frozen"):
        return False
    if (verified.get("settings") or {}).get("depth") not in (None, setting[0]):
        return False
    try:
        vp = json.loads(git("show", "%s:%s" % (verified["frozen"], src), cwd=root)).get("value_play", {}) or {}
    except Exception:
        return False
    return (vp.get("mull_gen_depth"), vp.get("mull_gen_budget_ms")) == tuple(setting)


_KCACHE = None


def keep_summary(H, prof, raw):
    """-> {buckets, commit, R, meta{depth,budget_ms}} of a keep table. Parsing one is ~3 s (176k entries),
    so the few fields used are cached by git blob id in logs/artifact_queue/keepcache.json; an
    uncommitted table is keyed by size+mtime instead."""
    global _KCACHE
    cp = os.path.join(ROOT, "logs", "artifact_queue", "keepcache.json")
    if _KCACHE is None:
        try:
            _KCACHE = json.load(open(cp))
        except Exception:
            _KCACHE = {}

    def key(path):
        if path in H.tree and not H.is_dirty(path):
            return H.tree[path]
        st = os.stat(os.path.join(H.root, path))
        return "%s:%d:%d" % (path, st.st_size, int(st.st_mtime))

    k = key(prof) + "|" + (key(raw) if raw and os.path.exists(os.path.join(H.root, raw)) else "-")
    if k not in _KCACHE:
        ek = read_json_any(os.path.join(H.root, prof)).get("exhaustive_keep", {})
        meta = {}
        if raw and os.path.exists(os.path.join(H.root, raw)):
            m = read_json_any(os.path.join(H.root, raw)).get("meta", {})
            meta = {"depth": m.get("depth"), "budget_ms": m.get("budget_ms"), "R": m.get("R")}
        _KCACHE[k] = {"buckets": ek.get("buckets", []), "commit": ek.get("commit", "?"),
                      "R": ek.get("effective_R", "?"), "meta": meta}
        os.makedirs(os.path.dirname(cp), exist_ok=True)
        tmp = cp + ".tmp"
        json.dump(_KCACHE, open(tmp, "w"))
        os.replace(tmp, cp)
    return _KCACHE[k]



def assess_deck(H, d, cfg, smap):
    """-> {kind: item} for the deck's artifacts that exist."""
    R = H.root
    rel_list = os.path.relpath(d.deck_file, R) if os.path.isabs(d.deck_file) else d.deck_file
    suite_key = smap.get(rel_list)
    cards = main_cards(os.path.join(R, rel_list))
    marks = {(m["deck"], m["kind"]): m for m in cfg.get("marks", [])}
    ledger = read_ledger(R, d.key)
    items = {}
    src_now = git("rev-parse", "%s:src" % H.rev, cwd=H.root).strip()

    def base_item(kind, path, adopt, notes, ident):
        dirty = H.is_dirty(path)
        # The drift baseline: the LATER of the adoption and the newest verdict recorded for THIS artifact
        # (same identity) -- see LEDGER_DIR. A verdict whose frozen commit is not in this history is ignored.
        base, verified = adopt, None
        for e in ledger:
            if e.get("kind") == kind and e.get("ident") == ident and e.get("verdict") in VERIFYING \
                    and e.get("frozen") in H.pos and (not base or H.pos[e["frozen"]] > H.pos[H.resolve(base)]):
                base, verified = e["frozen"], e
        ap = H.pos[H.resolve(base)] if base else len(H.order) - 1
        ch = play_churn(H, suite_key, ap) if suite_key else None
        src_commits = H.src_commits_since(base) if base else 0
        it = {"deck": d.key, "dir": d.dir, "stem": d.stem, "kind": kind, "path": path,
              "adopted": adopt, "adopted_date": H.date(adopt), "uncommitted": dirty or not adopt,
              "baseline": base, "baseline_date": H.date(base), "verified": verified,
              "ledger_runs": sorted(set(e.get("run_id") for e in ledger if e.get("run_id"))),
              "suite_key": suite_key, "churn": ch, "src_commits_since": src_commits,
              "flags": [], "notes": notes, "blob": ident or "uncommitted"}
        if verified:
            it["notes"].append("VERIFIED %s at %s: %s -- drift is measured from there" % (
                verified.get("date", "?"), verified["frozen"][:9], verified.get("detail", verified["verdict"])))
        cdc = card_data_changes(H, base, cards) if base else []
        if cdc:
            it["flags"].append(("card-data", "cards.json changed for %d deck card(s): %s"
                                % (len(cdc), ", ".join(cdc[:6]) + (" ..." if len(cdc) > 6 else ""))))
        mk = marks.get((d.key, kind))
        ans = mark_answer(mk, kind, ledger) if mk else None
        if ans:
            it["mark_answered"] = ans
            it["notes"].append("USER MARK p%s (%s) ANSWERED by run %s: %s -- `unmark` it to tidy" % (
                mk.get("priority", 0), mk.get("set") or mk.get("date"), ans["run_id"],
                ans.get("detail", ans["verdict"])))
        elif mk:
            it["mark"] = mk
        if not suite_key:
            it["blocked"] = "not in test/regression_cases.sh (both generators refuse: exit 3)"
        if d.key in cfg.get("exclude", []):
            it["blocked"] = "excluded in %s" % CONFIG_REL
        return it

    # ---- keep table
    prof, raw = keep_paths(d, R)
    if prof:
        adopt = H.last_touch(prof)
        it = base_item("mulligan", prof, adopt, [], H.tree.get(prof) if not H.is_dirty(prof) else None)
        try:
            kt = keep_summary(H, prof, raw)
            bucketed = set(n for b in kt["buckets"] for n in b)
            alias = {a.get("name"): a.get("as") for a in
                     json.load(open(os.path.join(R, d.profile))).get("mulligan", {}).get("keep_table_alias", [])}
            missing = sorted(c for c in cards if c not in bucketed)
            gone = sorted(c for c in bucketed if c not in cards)
            if missing or gone:
                txt = []
                if missing:
                    txt.append("list cards with NO bucket: " + ", ".join(
                        "%s%s" % (c, " (aliased as %s, LOSSY)" % alias[c] if c in alias else "") for c in missing))
                if gone:
                    txt.append("bucketed cards no longer in the list: " + ", ".join(gone))
                it["flags"].append(("decklist", "; ".join(txt)))
            it["notes"].append("generated at %s, R=%s" % (kt["commit"], kt["R"]))
            meta = kt["meta"]
            (sd, sb), ssrc = screened_setting(R, {"value": d.value, "profile": d.profile})
            if meta.get("depth") is not None and sd is not None:
                if (meta.get("depth"), meta.get("budget_ms")) != (sd, sb):
                    msg = "generated at d%s b%s, deck now screens d%s b%s" % (
                        meta.get("depth"), meta.get("budget_ms"), sd, sb)
                    if settings_answered(it.get("verified"), ssrc, (sd, sb), H.root):
                        # A regeneration AT the screened setting already ran against this very table and kept
                        # it: the mismatch is answered. Flagging it again re-queued the same job forever
                        # (2026-10-29: Fluctuator kept-incumbent three times in one day, 15:38 / 19:05 / 20:07).
                        it["notes"].append("settings: %s -- answered by the %s verdict at %s" % (
                            msg, it["verified"]["verdict"], it["verified"]["frozen"][:9]))
                    else:
                        it["flags"].append(("settings", msg))
            inc_R = max([r for r in (kt.get("R"), meta.get("R")) if isinstance(r, int)] or [0]) or None
            plan = mull_plan(cfg, d.key, inc_R, meta.get("depth"), meta.get("budget_ms"), (sd, sb))
            it["plan"] = plan
            it["left_be"] = plan["beyond"]
            it["notes"].append("regen plan: recipe %s%s; %s" % (
                plan["recipe"], "".join(" %s=%s" % kv for kv in plan["env"].items()),
                "; ".join(plan["why"]) or "screened setting as-is"))
        except Exception as e:                                  # an unreadable artifact is itself a finding
            it["flags"].append(("unreadable", "could not read %s: %s" % (prof, e)))
        items["mulligan"] = it

    # ---- value leaf (only a sidecar carrying an eval_model is a generated leaf; a settings-only
    # sidecar such as Snow's leaf:'none' has nothing to regenerate unless the USER marks it)
    vpath = os.path.join(R, d.value)
    if os.path.exists(vpath):
        V = json.load(open(vpath))
        h_now = eval_model_hash(V)
        if h_now or (d.key, "valueleaf") in marks:
            adopt = None
            for c in H.touches(d.value):                    # newest first; walk back while the model is unchanged
                b = H.blobs.get(c, d.value)
                try:
                    hh = eval_model_hash(json.loads(b.decode())) if b else None
                except Exception:
                    hh = None
                if hh != h_now:
                    break
                adopt = c
            notes = []
            prov = V.get("provenance", {})
            if prov.get("regenerated_commit"):
                notes.append("trained at %s on %s rows" % (prov.get("regenerated_commit"), prov.get("rows", "?")))
            if not h_now:
                notes.append("settings-only sidecar (no eval_model): ships NO leaf")
            it = base_item("valueleaf", d.value, adopt or H.last_touch(d.value), notes,
                           ("em:" + h_now[:20]) if h_now else None)
            km = items.get("mulligan")
            if km and km.get("adopted") and it.get("adopted") and \
                    H.pos[H.resolve(km["adopted"])] > H.pos[H.resolve(it["adopted"])]:
                it["flags"].append(("pipeline-order", "keep table re-adopted %s, after the leaf was fitted (%s)"
                                    % (km["adopted_date"], it["adopted_date"])))
            items["valueleaf"] = it
    for it in items.values():
        it["src_tree"] = src_now
        score(it, cfg)
    return items


# Exact axes that the GT drift CANNOT see get a weight; ones whose effect on play already shows up in the
# measured drift (a card-data edit, a re-ordered pipeline) are reported but not double-counted.
FLAG_WEIGHT = {"decklist": 1.0, "unreadable": 1.0, "settings": 0.1, "card-data": 0.0, "pipeline-order": 0.0}


def score(it, cfg):
    """Drift score = mean per-game |win-turn change| on the deck's suite games since the artifact went
    live (turns), plus a fixed weight per exact flag, times the kind weight. A USER mark always sorts
    first (by its priority). NOT the digest churn: that saturates (50-99% for nearly every artifact,
    since any play change anywhere moves a game's decision stream) and cannot tell Knights (92% churn,
    0.010 turns) from Hinata2 (97%, 1.09 turns). Measured 2026-10-23 on the first full status run."""
    s = it["churn"]["abs_dturn"] if it.get("churn") else 0.0
    for f, _ in it["flags"]:
        s += FLAG_WEIGHT.get(f, 0.1)
    s *= float(cfg["kind_weight"].get(it["kind"], 1.0))
    it["score"] = round(s, 4)
    it["queued"] = bool(it.get("mark")) or (s >= float(cfg["min_drift"]) and not it.get("uncommitted"))
    if it.get("blocked") or (it.get("left_be") and not it.get("mark")):
        it["queued"] = False
    it["sort"] = (0 if it.get("mark") else 1, -int(it.get("mark", {}).get("priority", 0)), -s)


def assess_all(root=ROOT, rev="HEAD", only=None, cfg=None):
    # The worker passes the cfg it loaded (the frozen tree has no config until the tool lands there):
    # reloading it here from `root` dropped every USER mark (2026-10-25, Dragons run ahead of Angels/Giants).
    cfg = cfg if cfg is not None else load_config(root)
    H = History(rev, root)
    smap = suite_map(root)
    out = []
    try:
        for d in shipping_decks(root):
            if only and d.key != only and d.dir.split("/")[-1] != only:
                continue
            out.extend(assess_deck(H, d, cfg, smap).values())
    finally:
        H.blobs.close()
    out.sort(key=lambda it: it["sort"])
    return out, cfg


def deck_jobs(items, cfg):
    """Fold artifact items into per-DECK jobs. Stages always run mulligan BEFORE valueleaf (USER order
    2026-10-18), and a deck is one claim so the leaf is fitted to the table this same job produced."""
    by = {}
    for it in items:
        by.setdefault(it["deck"], []).append(it)
    jobs = []
    for deck, its in by.items():
        q = [i for i in its if i["queued"]]
        if not q:
            continue
        kinds = set(i["kind"] for i in q)
        if "mulligan" in kinds and cfg.get("leaf_follows_mulligan") and any(i["kind"] == "valueleaf" for i in its):
            kinds.add("valueleaf")
        best = min(q, key=lambda i: i["sort"])
        jobs.append({"deck": deck, "dir": best["dir"], "stages": [k for k in KINDS if k in kinds],
                     "sort": best["sort"], "score": max(i["score"] for i in q),
                     "blobs": {i["kind"]: i["blob"] for i in its},
                     "queued_kinds": sorted(set(i["kind"] for i in q)),
                     "ledger_runs": sorted(set(r for i in its for r in i.get("ledger_runs", []))),
                     "suite_key": best.get("suite_key")})
    jobs.sort(key=lambda j: j["sort"])
    return jobs


# --------------------------------------------------------------------------------------------- report
def fmt_item(it):
    ch = it.get("churn")
    if ch and ch["games"]:
        c = "|dt| %.3f  net %+.3f  (%4.1f%% of %5d changed)" % (ch["abs_dturn"], ch["net_dturn"],
                                                              100 * ch["churn"], ch["games"])
    elif ch:
        c = "no GT accepted since adoption (%d cases)" % ch["cases"]
    else:
        c = "no suite cases"
    tags = []
    if it.get("mark"):
        tags.append("MARKED(p%s)" % it["mark"].get("priority", 0))
    if it.get("mark_answered"):
        tags.append("mark-answered(unmark)")
    tags += [f for f, _ in it["flags"]]
    if it.get("uncommitted"):
        tags.append("uncommitted")
    if it.get("blocked"):
        tags.append("BLOCKED")
    return "%-6s %-24s %-9s %6.3f  %-10s %-48s %s" % (
        "QUEUE" if it["queued"] else "", it["deck"][:24], it["kind"], it["score"], it["adopted_date"], c,
        " ".join(tags))


def cmd_status(a):
    items, cfg = assess_all()
    if a.json:
        print(json.dumps([{k: v for k, v in it.items() if k != "sort"} for it in items], indent=1))
        return 0
    print("%-6s %-24s %-9s %6s  %-10s %-48s %s" % ("", "deck", "artifact", "score", "adopted",
                                                    "suite-GT play drift since adoption or last verdict", "flags"))
    shown = 0
    for it in items:
        if a.all or it["queued"] or it.get("flags") or it.get("mark") or it.get("mark_answered"):
            print(fmt_item(it))
            shown += 1
    hidden = len(items) - shown
    if hidden:
        print("(%d fresh artifact(s) below min_drift=%.3f turns hidden; --all shows them)" % (hidden, cfg["min_drift"]))
    jobs = deck_jobs(items, cfg)
    claims = Queue().claims()                 # the LOCAL copy of origin/artifact-queue (`claims` fetches)
    note_claim_drift(ROOT, "HEAD", jobs, claims)
    now = utcnow()
    print("\nDECK JOBS, in claim order (%d):" % len(jobs))
    for i, j in enumerate(jobs, 1):
        c = claims.get(j["deck"])
        tag = ""
        if c and claim_blocks(c, j, cfg, None, now):
            ds = (j.get("drift_since") or {}).get(c.get("frozen"))
            tag = "  [%s by %s%s%s]" % (c.get("state"), c.get("worker"),
                                        ", awaiting landing: " + c["result_branch"] if c.get("result_branch") else "",
                                        ", drift since %s %.4f < %.3f" % (c["frozen"][:9], ds, cfg["min_drift"])
                                        if ds is not None else "")
        print("  %2d. %-24s %s%s" % (i, j["deck"], " -> ".join(j["stages"]), tag))
    return 0


def cmd_explain(a):
    items, cfg = assess_all(only=a.deck)
    if not items:
        print("no artifacts found for %s" % a.deck)
        return 1
    for it in items:
        print("== %s / %s  (%s)" % (it["deck"], it["kind"], it["path"]))
        print("   adopted    : %s %s%s" % (it["adopted_date"], (it["adopted"] or "-")[:9],
                                          "  (UNCOMMITTED changes)" if it["uncommitted"] else ""))
        if it.get("baseline") and it["baseline"] != it["adopted"]:
            print("   drift from : %s %s (the last VERIFICATION, later than the adoption)" % (
                it["baseline_date"], it["baseline"][:9]))
        print("   suite key  : %s" % (it["suite_key"] or "-- not in the suite"))
        ch = it["churn"]
        if ch:
            print("   play churn : %d/%d games changed decisions (%.1f%%) over %d/%d cases with a post-adoption accept;"
                  " mean |dturn| %.3f, net %+.4f" % (ch["changed"], ch["games"], 100 * ch["churn"],
                                                     ch["cases_measured"], ch["cases"], ch["abs_dturn"], ch["net_dturn"]))
        print("   src commits since adoption: %d" % it["src_commits_since"])
        for f, t in it["flags"]:
            print("   FLAG %-14s %s" % (f, t))
        for n in it["notes"]:
            print("   note       : %s" % n)
        if it.get("mark"):
            print("   USER MARK  : p%s -- %s" % (it["mark"].get("priority", 0), it["mark"].get("why", "")))
        if it.get("blocked"):
            print("   BLOCKED    : %s" % it["blocked"])
        print("   score %.4f  -> %s" % (it["score"], "QUEUED" if it["queued"] else "not queued"))
    return 0


def cmd_mark(a):
    if a.kind not in KINDS:
        print("kind must be one of %s" % ", ".join(KINDS))
        return 2
    cfg = load_config()
    a.deck = deck_registry.slug(a.deck)       # 'Snow' / 'Mirrorwing Dragon' -> the registry key
    if a.deck not in set(d.key for d in shipping_decks()):
        print("no shipping deck with key %r (see `status --all`)" % a.deck)
        return 2
    marks = [m for m in cfg.get("marks", []) if not (m["deck"] == a.deck and m["kind"] == a.kind)]
    if a.cmd == "mark":
        marks.append({"deck": a.deck, "kind": a.kind, "why": a.why, "priority": a.priority,
                      "date": datetime.date.today().isoformat(), "set": iso(utcnow())})
    save_marks(marks)
    print("%s %s/%s in %s -- commit + push it so every worker sees it" % (
        "marked" if a.cmd == "mark" else "unmarked", a.deck, a.kind, CONFIG_REL))
    return 0


# ---------------------------------------------------------------------------------------- the queue
def utcnow():
    return datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0)


def iso(t):
    return t.strftime("%Y-%m-%dT%H:%M:%SZ")


def parse_iso(s):
    return datetime.datetime.strptime(s, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=datetime.timezone.utc)


class Queue(object):
    """Claims live on origin's `artifact-queue` branch as claims/<deck>.json. Every write is a commit on
    top of the tip we read, pushed WITHOUT force, so a concurrent writer makes the push fail and we
    re-read and retry -- a compare-and-swap on the whole queue. Written with git plumbing only: no
    working tree, no index, nobody's branch touched (the references-autosave pattern)."""

    def __init__(self, root=ROOT, remote="origin"):
        self.root = root
        self.remote = remote
        self.lock = threading.RLock()     # the heartbeat thread and the job share this queue

    def fetch(self):
        git("fetch", "-q", self.remote, "+refs/heads/%s:refs/remotes/%s/%s" % (QUEUE_BRANCH, self.remote, QUEUE_BRANCH),
            cwd=self.root, check=False)

    def tip(self):
        return git("rev-parse", "-q", "--verify", "refs/remotes/%s/%s" % (self.remote, QUEUE_BRANCH),
                   cwd=self.root, check=False).strip() or None

    def claims(self):
        t = self.tip()
        if not t:
            return {}
        out = {}
        for line in git("ls-tree", t, "claims/", cwd=self.root).splitlines():
            meta, path = line.split("\t", 1)
            blob = meta.split()[2]
            try:
                c = json.loads(git("cat-file", "blob", blob, cwd=self.root))
                out[c["deck"]] = c
            except Exception:
                pass
        return out

    def _write(self, deck, claim, expect=None):
        """CAS write of claims/<deck>.json. `expect(current_claim_or_None) -> bool` must hold on the tip we
        build on. -> True on success, False if the precondition failed."""
        with self.lock:
            return self._write_locked(deck, claim, expect)

    def _write_locked(self, deck, claim, expect):
        for _ in range(8):
            self.fetch()
            t = self.tip()
            cur = self.claims().get(deck)
            if expect is not None and not expect(cur):
                return False
            entries = []
            if t:
                for line in git("ls-tree", t, "claims/", cwd=self.root).splitlines():
                    meta, path = line.split("\t", 1)
                    if path != "claims/%s.json" % deck:
                        entries.append("%s\t%s" % (meta, os.path.basename(path)))
            blob = git("hash-object", "-w", "--stdin", cwd=self.root,
                       inp=json.dumps(claim, indent=1, sort_keys=True) + "\n").strip()
            entries.append("100644 blob %s\t%s.json" % (blob, deck))
            sub = git("mktree", cwd=self.root, inp="\n".join(sorted(entries, key=lambda e: e.split("\t")[1])) + "\n").strip()
            top = git("mktree", cwd=self.root, inp="040000 tree %s\tclaims\n" % sub).strip()
            args = ["commit-tree", top, "-m", "artifact-queue: %s %s by %s" % (deck, claim.get("state"), claim.get("worker"))]
            if t:
                args[2:2] = ["-p", t]
            c = git(*args, cwd=self.root).strip()
            r = subprocess.run(["git", "push", "-q", self.remote, "%s:refs/heads/%s" % (c, QUEUE_BRANCH)],
                               cwd=self.root, capture_output=True, text=True)
            if r.returncode == 0:
                git("update-ref", "refs/remotes/%s/%s" % (self.remote, QUEUE_BRANCH), c, cwd=self.root)
                return True
            time.sleep(3)                       # someone else wrote first: re-read, re-check, retry
        raise RuntimeError("could not update %s after 8 attempts" % QUEUE_BRANCH)


RETRY_ON_DRIFT = ("rejected", "refused", "done")


def note_claim_drift(root, rev, jobs, claims):
    """Stamp job["drift_since"] = {frozen: mean per-game |dturn|} for every job whose claim finished without
    a result to land, measured on the deck's suite GT since the commit that claim froze (claim_blocks reads
    it). One History walk, and only when some job needs it."""
    need = []
    for j in jobs:
        c = claims.get(j["deck"])
        if c and c.get("state") in RETRY_ON_DRIFT and c.get("frozen") and c.get("blobs") == j["blobs"] \
                and not c.get("result_branch") and j.get("suite_key"):
            need.append((j, c))
    if not need:
        return
    H = History(rev, root)
    try:
        for j, c in need:
            if c["frozen"] not in H.pos and not git("cat-file", "-t", c["frozen"], cwd=root, check=False).strip():
                continue                      # frozen commit unknown here: unmeasured, so it holds
            ch = play_churn(H, j["suite_key"], H.pos[H.resolve(c["frozen"])])
            j["drift_since"] = {c["frozen"]: round(ch["abs_dturn"], 4)}
    finally:
        H.blobs.close()


def claim_blocks(c, job, cfg, worker, now):
    """Does an existing claim keep `worker` off this deck job?"""
    if not c:
        return False
    if c.get("run_id") and c["run_id"] in job.get("ledger_runs", ()):
        return False                          # its verdict has LANDED: the ledger now governs this deck
    st = c.get("state")
    if st == "paused":
        # A USER-paused job (`pause`): set aside for EVERY other worker, with no lease expiry, and resumed only
        # by the worker that holds its journal and inflight.json (USER 2026-10-26).
        return c.get("worker") != worker
    if st in ("claimed", "running"):
        if c.get("worker") == worker:
            return False
        hb = parse_iso(c.get("heartbeat", c.get("started")))
        return (now - hb).total_seconds() < cfg["lease_hours"] * 3600
    if st in ("done", "rejected", "failed", "refused"):
        # A finished claim holds while the artifacts it ran against are still the shipped ones: the
        # result branch has not landed yet.
        same = c.get("blobs") == job["blobs"]
        if not same:
            return False
        if st in ("done", "rejected") and c.get("result_branch"):
            return True                       # a result/verdict is waiting to be landed: do not redo it
        if st == "failed":
            # A crash says nothing about the deck (and a tool fix moves no play): retry after a window.
            return (now - parse_iso(c.get("finished", c.get("started")))).days < cfg["retry_rejected_days"]
        # rejected / refused, or done with nothing adopted (a leaf left staged). USER 2026-10-30: "If we reject
        # or refuse it should wait until the drift has widened from the new version that we built from (at
        # least some amount)" -- a re-run on unchanged play would only repeat the verdict. So it waits for
        # min_drift turns of suite drift since the claim's FROZEN commit, as measured by note_claim_drift();
        # the same bar as first entry (USER: "them being the same is probably fine"). A job nobody measured
        # (no suite cases, or a caller that did not ask) holds.
        d = (job.get("drift_since") or {}).get(c.get("frozen"))
        return d is None or d < float(cfg["min_drift"])
    return False


def cmd_claims(a):
    q = Queue()
    q.fetch()
    cs = q.claims()
    if not cs:
        print("no claims on origin/%s" % QUEUE_BRANCH)
        return 0
    now = utcnow()
    for deck, c in sorted(cs.items()):
        hb = c.get("heartbeat") or c.get("finished") or c.get("started")
        age = (now - parse_iso(hb)).total_seconds() / 3600 if hb else 0
        print("%-24s %-9s %-24s stages=%-22s stage=%-10s %5.1fh ago  %s" % (
            deck, c.get("state"), c.get("worker"), ",".join(c.get("stages", [])), c.get("stage", "-"), age,
            c.get("result_branch") or c.get("detail", "")))
    return 0


# ------------------------------------------------------------------------------------------- worker
def worker_id():
    """MDT_WORKER_ID, else an id minted once per CHECKOUT and kept in logs/artifact_queue/WORKER_ID. Not the
    bare hostname: in a devcontainer that is the container id, which changes on every rebuild and would
    orphan an in-flight job's resume state (and its worktree path)."""
    if os.environ.get("MDT_WORKER_ID"):
        return re.sub(r"[^A-Za-z0-9_.-]", "_", os.environ["MDT_WORKER_ID"])
    p = os.path.join(ROOT, "logs", "artifact_queue", "WORKER_ID")
    if os.path.exists(p):
        return open(p).read().strip()
    os.makedirs(os.path.dirname(p), exist_ok=True)
    wid = "%s-%s" % (re.sub(r"[^A-Za-z0-9_.-]", "_", os.path.basename(ROOT)), hashlib.sha1(
        ("%s%s" % (socket.gethostname(), time.time())).encode()).hexdigest()[:6])
    open(p, "w").write(wid + "\n")
    return wid


def state_dir(wid):
    p = os.path.join(ROOT, "logs", "artifact_queue", wid)
    os.makedirs(p, exist_ok=True)
    return p


def log(wid, msg):
    line = "[%s] %s" % (iso(utcnow()), msg)
    print(line, flush=True)
    with open(os.path.join(state_dir(wid), "worker.log"), "a") as f:
        f.write(line + "\n")


def descendants(pid):
    kids = {}
    for p in os.listdir("/proc"):
        if not p.isdigit():
            continue
        try:
            ppid = int(open("/proc/%s/stat" % p).read().rsplit(")", 1)[1].split()[1])
        except Exception:
            continue
        kids.setdefault(ppid, []).append(int(p))
    out, todo = set(), [pid]
    while todo:
        x = todo.pop()
        for k in kids.get(x, []):
            if k not in out:
                out.add(k)
                todo.append(k)
    return out


def foreign_engines():
    """-> [(pid, cmdline)] of mtg / mtg-analyze processes that are NOT ours. Only this container's
    processes are visible: a generation in a sibling container sharing the host is not."""
    mine = descendants(os.getpid()) | {os.getpid()}
    out = []
    for p in os.listdir("/proc"):
        if not p.isdigit() or int(p) in mine:
            continue
        try:
            comm = open("/proc/%s/comm" % p).read().strip()
            if comm in ("mtg", "mtg-analyze"):
                cmd = open("/proc/%s/cmdline" % p).read().replace("\0", " ")[:160]
                out.append((int(p), cmd))
        except Exception:
            pass
    return out


def busy_reason(cfg):
    """-> why the box is busy, or None. Two signals: a foreign mtg / mtg-analyze in THIS container, and the
    1-min load average -- which in WSL2 is the whole VM's, so it also sees a generation running in a sibling
    container that the process scan cannot. Only consulted between stages, when none of our own work runs."""
    f = foreign_engines()
    if f:
        return "%d foreign engine process(es), e.g. pid %d: %s" % (len(f), f[0][0], f[0][1])
    lim = cfg.get("idle_max_load") or 0.5 * (os.cpu_count() or 1)
    la = os.getloadavg()[0]
    if la > lim:
        return "load average %.1f > %.1f (another container may be running a generation)" % (la, lim)
    return None


def wait_idle(wid, cfg):
    """Block until the box is free (busy_reason). -> True if it had to wait (the caller then re-ranks)."""
    warned = False
    while True:
        why = busy_reason(cfg)
        if not why:
            return warned
        if not warned:
            log(wid, "box busy (%s) -- waiting; nothing runs alongside another run" % why)
            warned = True
        if stop_requested(wid):
            return warned
        time.sleep(cfg["idle_poll_minutes"] * 60)


def stop_requested(wid):
    return os.path.exists(os.path.join(state_dir(wid), "STOP"))


def worktree_path(wid):
    return os.path.join(ROOT, ".claude", "worktrees", "artifact-worker-" + wid)


def sync_worktree(wid, cfg):
    """Freeze the worker's worktree on origin/<branch>'s tip. Never discards anything: tracked edits left
    over from an interrupted job are committed to a rescue branch first."""
    wt = worktree_path(wid)
    br = cfg["branch"]
    git("fetch", "-q", "origin", br)
    target = git("rev-parse", "origin/%s" % br).strip()
    if not os.path.exists(os.path.join(wt, ".git")):
        os.makedirs(os.path.dirname(wt), exist_ok=True)
        git("worktree", "add", "--detach", wt, target)
        return wt, target
    if git("status", "--porcelain", "--untracked-files=no", cwd=wt).strip():
        rescue = "%srescue-%s-%s" % (RESULT_PREFIX, wid, utcnow().strftime("%Y%m%dT%H%M%S"))
        git("checkout", "-q", "-b", rescue, cwd=wt)
        git("commit", "-q", "-am", "artifact-queue: rescue tracked edits left in the worker worktree", cwd=wt)
        git("push", "-q", "origin", rescue, cwd=wt, check=False)
        log(wid, "worktree had tracked edits: committed to %s (pushed) before re-syncing" % rescue)
    git("checkout", "-q", "--detach", target, cwd=wt)
    return wt, target


def run_logged(wid, cmd, cwd, logname, env=None):
    p = os.path.join(state_dir(wid), logname)
    log(wid, "  $ %s   (cwd %s, log %s)" % (" ".join(cmd), cwd, p))
    with open(p, "a") as f:
        f.write("\n=== %s  %s\n" % (iso(utcnow()), " ".join(cmd)))
        f.flush()
        r = subprocess.run(cmd, cwd=cwd, stdout=f, stderr=subprocess.STDOUT, env=env)
    return r.returncode


def ungzip_incumbent(wt, d):
    """mullgen.sh decides REGENERATION vs FIRST VERSION by `[ -e <stem>.keepmodel.exhaustive.profile.json ]`
    -- the PLAIN name -- but every deck commits only the .gz. Left alone, a regen would (1) save no
    incumbent, so it validates with the first-version bar (keep vs static) instead of old-vs-new, and
    (2) leave the new plain table SHADOWED by the old .gz, which the engine loads first. So in the
    worker's own worktree the incumbent is unpacked to the plain names first. -> list of (gz, plain).

    The PROFILE only. The plain raw name is the generator's FLOOR CHECKPOINT (cfg.out_raw): an existing
    file there is resumed when its fingerprints match, so an unpacked incumbent raw from an unchanged
    play digest is "resumed" as if it were this run's interrupted floor pass and the refine never runs
    (Colossus Hammer 2026-10-25: a 5-minute "regeneration" at R~3 per hand, 0.100 t worse, rejected).
    The raw stays .gz; mullgen.sh and the plan below read it from there."""
    done = []
    for part in ("profile",):
        gz = os.path.join(wt, d["dir"], "%s.keepmodel.exhaustive.%s.json.gz" % (d["stem"], part))
        plain = gz[:-3]
        if os.path.exists(gz) and not os.path.exists(plain):
            with gzip.open(gz, "rb") as fi, open(plain, "wb") as fo:
                shutil.copyfileobj(fi, fo)
            os.remove(gz)
            done.append((gz, plain))
    return done


def regzip(wt, d):
    """After the driver: put the plain artifacts back into their committed .gz form. Unchanged content
    restores the original .gz byte-for-byte (git checkout) so a rejected regen leaves no diff."""
    for part in ("profile", "raw"):
        gz = os.path.join(wt, d["dir"], "%s.keepmodel.exhaustive.%s.json.gz" % (d["stem"], part))
        plain = gz[:-3]
        if not os.path.exists(plain):
            continue
        rel = os.path.relpath(gz, wt)
        old = subprocess.run(["git", "show", "HEAD:%s" % rel], cwd=wt, capture_output=True)
        if old.returncode == 0 and gzip.decompress(old.stdout) == open(plain, "rb").read():
            os.remove(plain)
            git("checkout", "-q", "HEAD", "--", rel, cwd=wt)
            continue
        with open(plain, "rb") as fi, gzip.GzipFile(gz, "wb", mtime=0) as fo:
            shutil.copyfileobj(fi, fo)
        os.remove(plain)


class MachineFault(RuntimeError):
    """A failure of the box (build, disk, git), not of the deck's regeneration."""


class Heartbeat(threading.Thread):
    def __init__(self, q, deck, claim, cfg):
        super().__init__(daemon=True)
        self.q, self.deck, self.claim, self.cfg = q, deck, claim, cfg
        self.ev = threading.Event()

    def run(self):
        while not self.ev.wait(self.cfg["heartbeat_minutes"] * 60):
            self.claim["heartbeat"] = iso(utcnow())
            try:
                self.q._write(self.deck, self.claim)
            except Exception:
                pass

    def stop(self):
        self.ev.set()


def put_claim(q, deck, claim, wid, job=None, cfg=None):
    """CAS write of our claim. The precondition is the picker's own rule (claim_blocks), so an expired lease
    the picker chose can actually be taken over; a mismatch here once meant pick -> lose -> pick forever."""
    claim["heartbeat"] = iso(utcnow())
    if job is None:                           # stage/heartbeat update of a claim we already hold
        return q._write(deck, claim, expect=lambda cur: (not cur) or cur.get("worker") == wid)
    return q._write(deck, claim, expect=lambda cur: not claim_blocks(cur, job, cfg, wid, utcnow()))


def versus_cost(wt, stem, since):
    """-> (mean per-seed new/old work-units ratio, its se, seeds) over THIS run's old-vs-new versus rounds, or None.
    Work units, not wall: they are thread- and load-invariant, so a shared box cannot tilt the verdict."""
    t0 = parse_iso(since).timestamp() if since else 0
    per = {}
    # mullgen.sh names the rounds off the SANITISED stem ("Melira Pod" -> Melira_Pod_r1); globbing the raw stem
    # found nothing for any deck with a space, and Melira Pod's 2026-10-28 table was recorded "cost not measured".
    safe = re.sub(r"[^A-Za-z0-9._-]", "_", stem)
    dirs = set(glob.glob(os.path.join(wt, "logs", "keepmodel_exh_versus_%s_r*" % glob.escape(safe))))
    for d in sorted(dirs | set(glob.glob(os.path.join(wt, "logs", "keepmodel_exh_versus_%s_r*" % glob.escape(stem))))):
        f = os.path.join(d, "batch.log")
        if not os.path.exists(f) or os.path.getmtime(f) < t0:
            continue
        for ln in open(f):
            m = re.match(r"s(\d+)@(old|new): played=\d+ .*units=(\d+)", ln)
            if m:
                per.setdefault(m.group(1), {})[m.group(2)] = int(m.group(3))
    r = [v["new"] / v["old"] for v in per.values() if v.get("old") and "new" in v]
    if len(r) < 2:
        return None
    mean = sum(r) / len(r)
    se = math.sqrt(sum((x - mean) ** 2 for x in r) / (len(r) - 1) / len(r))
    return mean, se, len(r)


def run_job(wid, cfg, q, job, wt, frozen, deckinfo, dry):
    deck = job["deck"]
    claim = {"deck": deck, "worker": wid, "state": "running", "stages": job["stages"], "stage": job["stages"][0],
             "frozen": frozen, "started": iso(utcnow()), "blobs": job["blobs"], "score": job["score"],
             "host": socket.gethostname(), "run_id": "%s-%s-%s" % (deck, wid, utcnow().strftime("%Y%m%dT%H%M%S"))}
    if dry:
        why = busy_reason(cfg)
        log(wid, "DRY RUN: would claim %s and run %s at %s%s" % (deck, " -> ".join(job["stages"]), frozen[:9],
            "; the box is BUSY now (%s), so a real run would wait" % why if why else "; the box is free"))
        return "dry"
    if not put_claim(q, deck, claim, wid, job, cfg):
        log(wid, "%s: another worker claimed it first -- skipping" % deck)
        return "lost"
    ifp = os.path.join(state_dir(wid), "inflight.json")
    prior = json.load(open(ifp)) if os.path.exists(ifp) else {}
    # A RESUMED job skips the stages it already finished: re-running `mullgen.sh run` after an adoption
    # would take the NEW table as the incumbent and regenerate it. The recipe is pinned with the job, so
    # a config edit between attempts cannot resume a journal at the wrong R (the 2026-10-02 loss).
    done_stages = prior.get("done_stages", [])
    detail = prior.get("detail", [])
    plan = prior.get("plan")                  # the mulligan settings, pinned once computed
    if prior.get("claim", {}).get("run_id"):
        claim["run_id"] = prior["claim"]["run_id"]

    def save_inflight():
        json.dump({"job": job, "claim": claim, "done_stages": done_stages, "detail": detail, "plan": plan,
                   "vl_queue_fresh": prior.get("vl_queue_fresh", False)}, open(ifp, "w"), indent=1)

    def verdict(kind, v, ident, extra):
        """Record a stage's verdict in the deck's ledger (in the worktree; it lands with the result branch)."""
        append_ledger(wt, deck, dict({"run_id": claim["run_id"], "kind": kind, "verdict": v, "ident": ident,
                                      "frozen": frozen, "src_tree": git("rev-parse", "%s:src" % frozen, cwd=wt).strip(),
                                      "date": datetime.date.today().isoformat(), "worker": wid}, **extra))

    save_inflight()
    hb = Heartbeat(q, deck, claim, cfg)
    hb.start()
    outcome = "done"
    try:
        if run_logged(wid, ["./build.sh"], wt, "build.log") != 0:
            raise MachineFault("./build.sh failed in %s (see build.log)" % wt)
        for stage in job["stages"]:
            if stage in done_stages:
                log(wid, "  %s already finished in an earlier attempt -- skipping" % stage)
                continue
            wait_idle(wid, cfg)
            claim["stage"] = stage
            try:
                put_claim(q, deck, claim, wid)
            except Exception as e:
                log(wid, "  (claim update failed, carrying on: %s)" % e)
            if stage == "mulligan":
                ungzip_incumbent(wt, deckinfo)
                base = os.path.join(wt, deckinfo["dir"], "%s.keepmodel.exhaustive." % deckinfo["stem"])
                if plan is None:
                    ek = read_json_any(base + "profile.json").get("exhaustive_keep", {})
                    rawp = next((base + n for n in ("raw.json", "raw.json.gz") if os.path.exists(base + n)), None)
                    meta = read_json_any(rawp).get("meta", {}) if rawp else {}
                    inc_R = max([r for r in (ek.get("effective_R"), meta.get("R")) if isinstance(r, int)] or [0]) or None
                    screened, _ = screened_setting(wt, deckinfo)
                    plan = mull_plan(cfg, deck, inc_R, meta.get("depth"), meta.get("budget_ms"), screened)
                    save_inflight()
                log(wid, "  mulligan plan: recipe %s %s; %s" % (plan["recipe"], plan["env"] or "", "; ".join(plan["why"])))
                # Raise the generation setting to the incumbent's IN THIS WORKTREE ONLY, restored after the
                # stage, so the result branch never changes a deck's shipped mull_gen_* (see mull_plan).
                _, sfile = screened_setting(wt, deckinfo)
                saved = None
                if plan["depth"] is not None and sfile:
                    sp = os.path.join(wt, sfile)
                    saved = open(sp, "rb").read()
                    J = json.loads(saved.decode())
                    J["value_play"]["mull_gen_depth"], J["value_play"]["mull_gen_budget_ms"] = plan["depth"], plan["budget"]
                    with open(sp, "w") as f:
                        json.dump(J, f, indent=1)
                env = dict(os.environ, **plan["env"])
                # This attempt's verdict must come from THIS attempt's report: a run that dies before the driver
                # rewrites VALIDATION.txt would otherwise be read off an earlier job's "VALIDATION PASSED".
                rep0 = os.path.join(wt, "logs", re.sub(r"[^A-Za-z0-9._-]", "_", deckinfo["dir"][len("decks/"):]) + "_mullgen",
                                    "VALIDATION.txt")
                if os.path.exists(rep0):
                    os.rename(rep0, rep0[:-4] + ".prev-%s.txt" % utcnow().strftime("%Y%m%dT%H%M%S"))
                try:
                    rc = run_logged(wid, ["bash", "scripts/mullgen.sh", "run", deckinfo["dir"], plan["recipe"]], wt,
                                    "%s.mulligan.log" % deck, env=env)
                finally:
                    if saved is not None:
                        open(os.path.join(wt, sfile), "wb").write(saved)
                regzip(wt, deckinfo)
                rep = os.path.join(wt, "logs", re.sub(r"[^A-Za-z0-9._-]", "_", deckinfo["dir"][len("decks/"):]) + "_mullgen",
                                   "VALIDATION.txt")
                text = open(rep).read() if os.path.exists(rep) else ""
                m = re.findall(r"new-vs-old delta: ([-+0-9.eE]+)t", text)
                delta = float(m[-1]) if m else None
                prof_rel = os.path.join(deckinfo["dir"], "%s.keepmodel.exhaustive.profile.json.gz" % deckinfo["stem"])
                settings = {"recipe": plan["recipe"], "env": plan["env"], "depth": plan["depth"], "budget": plan["budget"]}
                saved_inc = "regeneration: incumbent saved" in text
                if rc != 3 and text and not saved_inc:
                    # The driver did not see an incumbent, so it validated with the FIRST-VERSION bar (keep vs
                    # static), not old-vs-new: whatever it decided is not a replacement verdict.
                    outcome = "failed"
                    detail.append("mulligan: FAILED -- the driver ran FIRST-VERSION validation (no incumbent saved), "
                                  "so this is not an old-vs-new verdict; rc=%d, see %s.mulligan.log" % (rc, deck))
                    break
                # BETTER OVERALL (USER 2026-10-26: "If the table is better overall (average quality and performance
                # not worse) it is adoptable"). The driver gates quality; the worker adds the performance half from
                # the same versus games. Significantly dearer at no-worse quality is a TRADE: the USER's call.
                vc = versus_cost(wt, deckinfo["stem"], claim.get("started"))
                cost_s = ("cost %.3fx (se %.3f, %d seeds)" % vc) if vc else "cost not measured"
                dearer = bool(vc) and vc[0] - 1.0 > 2 * vc[1]
                cheaper = bool(vc) and 1.0 - vc[0] > 2 * vc[1]
                if rc == 0 and "VALIDATION PASSED" in text and dearer:
                    detail.append("mulligan: TRADE -- quality not worse (new-vs-old %s t) but the new table costs MORE "
                                  "(%s): the USER's call; left live on the result branch, NOT landed" % (delta, cost_s))
                    verdict("mulligan", "trade", artifact_ident("mulligan", wt, prof_rel),
                            {"delta": delta, "cost": vc, "settings": settings, "detail": detail[-1]})
                elif rc == 0 and "VALIDATION PASSED" in text:
                    detail.append("mulligan: ADOPTED (new-vs-old %s t, %s, recipe %s)" % (delta, cost_s, plan["recipe"]))
                    verdict("mulligan", "adopted", artifact_ident("mulligan", wt, prof_rel),
                            {"delta": delta, "cost": vc, "settings": settings, "detail": detail[-1]})
                elif rc == 0 and "UNGATED run" in text:
                    detail.append("mulligan: UNGATED -- the A/B was underpowered, the new table was left live "
                                  "with its verdict recorded (REVIEW before landing; recipe %s)" % plan["recipe"])
                    verdict("mulligan", "adopted-ungated", artifact_ident("mulligan", wt, prof_rel),
                            {"delta": delta, "settings": settings, "detail": detail[-1]})
                elif rc == 3:
                    outcome = "refused"
                    detail.append("mulligan: REFUSED by a driver gate (exit 3) -- see %s.mulligan.log" % deck)
                    break
                elif "VALIDATION FAILED -- new profile is worse than the old" in text:
                    outcome = "rejected"
                    detail.append("mulligan: KEPT INCUMBENT -- a fresh table at no-lower settings was not better "
                                  "(new-vs-old %s t, %s); incumbent restored%s" % (delta, cost_s,
                                  " -- a TRADE (worse but CHEAPER): reported for the USER" if cheaper else ""))
                    verdict("mulligan", "kept-incumbent", job["blobs"].get("mulligan"),
                            {"delta": delta, "cost": vc, "trade": cheaper, "settings": settings, "detail": detail[-1]})
                    # A leaf that is only here BECAUSE the table was regenerated (leaf_follows_mulligan) would be
                    # fitted to an unchanged table: skip it. A leaf queued on its OWN drift is still due -- the
                    # kept table is the one it is fitted to (2026-10-25: Anti-Lifegain's leaf was skipped here).
                    if "valueleaf" not in job.get("queued_kinds", job["stages"]):
                        break
                else:
                    outcome = "failed"
                    detail.append("mulligan: FAILED rc=%d -- see %s.mulligan.log" % (rc, deck))
                    break
            else:
                # valueleaf.sh resumes from logs/vlq_<key> markers. A NEW job must not inherit the markers,
                # rows and freeze of an earlier, finished job on this deck: move them aside (never delete).
                vlq = os.path.join(wt, "logs", "vlq_" + deck)
                if os.path.isdir(vlq) and not prior.get("vl_queue_fresh"):
                    aside = "%s.prev-%s" % (vlq, utcnow().strftime("%Y%m%dT%H%M%S"))
                    os.rename(vlq, aside)
                    log(wid, "  moved the earlier value-leaf queue aside -> %s" % aside)
                prior["vl_queue_fresh"] = True
                save_inflight()
                rc = run_logged(wid, ["bash", "scripts/valueleaf.sh", "run", deckinfo["dir"]], wt,
                                "%s.valueleaf.log" % deck)
                vtxt = open(os.path.join(state_dir(wid), "%s.valueleaf.log" % deck)).read()
                vtxt = vtxt[vtxt.rfind("\n=== 20"):]                  # this attempt's output only
                if rc == 0 and "=== COMPLETE" not in vtxt:
                    outcome = "failed"
                    detail.append("valueleaf: exit 0 WITHOUT the COMPLETE line -- not a verdict; see %s.valueleaf.log" % deck)
                    break
                if rc == 3:
                    outcome = "refused"
                    detail.append("valueleaf: REFUSED (exit 3)")
                    break
                if rc != 0:
                    outcome = "failed"
                    detail.append("valueleaf: FAILED rc=%d" % rc)
                    break
                new_ident = artifact_ident("valueleaf", wt, deckinfo["value"])
                adopted = new_ident != job["blobs"].get("valueleaf")
                dec = os.path.join(wt, "logs", "vlq_" + deck, "DECISIONS.txt")
                dec_tail = open(dec).read().strip().splitlines()[-6:] if os.path.exists(dec) else []
                detail.append("valueleaf: %s" % ("ADOPTED (live sidecar's eval_model replaced)" if adopted else
                                                 "KEPT INCUMBENT -- the regenerated leaf was not adopted (left staged "
                                                 "in the worker's logs/eval; see logs/vlq_%s/DECISIONS.txt)" % deck))
                verdict("valueleaf", "adopted" if adopted else "kept-incumbent",
                        new_ident if adopted else job["blobs"].get("valueleaf"),
                        {"detail": detail[-1], "decisions": dec_tail})
            done_stages.append(stage)
            save_inflight()
    except MachineFault as e:
        # Not the deck's fault: release the claim (so another machine can take it) and stop THIS worker,
        # rather than stamping every deck in the queue "failed" for retry_rejected_days one by one.
        hb.stop()
        claim.update({"state": "released", "finished": iso(utcnow()), "detail": "machine fault: %s" % e})
        q._write(deck, claim)
        os.remove(ifp)
        log(wid, "%s RELEASED -- machine fault: %s. Worker stopping; fix the box and `start` again." % (deck, e))
        raise
    except Exception as e:
        outcome = "failed"
        detail.append("worker error: %s" % e)
    finally:
        hb.stop()
    # Commit whatever the drivers left in the deck folder onto a result branch -- adopted artifacts, a
    # quarantined .DISABLED table, a screened mull_gen setting. Nothing is discarded, ever.
    branch = None
    ledger_rel = os.path.relpath(ledger_path(wt, deck), wt)
    if git("status", "--porcelain", "--", deckinfo["dir"], ledger_rel, cwd=wt).strip():
        branch = "%s%s-%s" % (RESULT_PREFIX, re.sub(r"[^A-Za-z0-9._-]", "_", deck), utcnow().strftime("%Y%m%d-%H%M"))
        git("checkout", "-q", "-b", branch, cwd=wt)
        git("add", "-A", "--", deckinfo["dir"], ledger_rel, cwd=wt)
        only_ledger = not git("diff", "--cached", "--name-only", "--", deckinfo["dir"], cwd=wt).strip()
        landing = ("VERDICT ONLY (no artifact changed, so no play change and no GT to accept): land it as-is so\n"
                   "the queue measures this deck's drift from the attempt and does not repeat it.\n\n"
                   if only_ledger else
                   "NOT LANDED: rebase onto the working branch, run smoke + regression and accept the GT the\n"
                   "new artifacts move (regression-testing.md), then push.\n\n")
        msg = ("artifact-refresh(%s): %s\n\nRegenerated by the artifact queue worker %s, frozen at %s.\n%s\n\n%s"
               "Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>\n"
               % (deck, outcome, wid, frozen[:9], "\n".join("  " + x for x in detail), landing))
        git("commit", "-q", "-m", msg, cwd=wt)
        r = subprocess.run(["git", "push", "-q", "origin", branch], cwd=wt, capture_output=True, text=True)
        if r.returncode != 0:
            detail.append("PUSH FAILED (%s) -- the commit is on local branch %s in %s" % (r.stderr.strip(), branch, wt))
    claim.update({"state": outcome, "finished": iso(utcnow()), "detail": "; ".join(detail), "result_branch": branch})
    q._write(deck, claim)
    os.remove(ifp)
    log(wid, "%s %s: %s%s" % (deck, outcome.upper(), "; ".join(detail), ("  -> " + branch) if branch else ""))
    # The adoption step: land the verdict now, between jobs (nothing else is generating on the box).
    # Held for the USER, never landed by the worker: a trade (dearer at no-worse quality) and an ungated table.
    hold = [x for x in detail if x.startswith("mulligan: TRADE") or x.startswith("mulligan: UNGATED")]
    if branch and hold:
        claim["land_note"] = "HELD for the USER: " + "; ".join(hold)
        q._write(deck, claim)
        log(wid, "LAND %s: HELD for the USER -- %s" % (branch, "; ".join(hold)))
    if branch and not hold and outcome in ("done", "rejected") and cfg.get("auto_land", True) and "PUSH FAILED" not in claim["detail"]:
        try:
            sha, note = land_result(wid, cfg, branch, deckinfo["dir"], ledger_only=(outcome == "rejected"))
        except Exception as e:
            sha, note = None, "landing error: %s -- left for an agent" % e
        log(wid, "LAND %s: %s%s" % (branch, ("landed %s -- " % sha[:9]) if sha else "NOT landed -- ", note))
        if sha:
            claim.update({"landed": sha, "landed_at": iso(utcnow())})
        else:
            claim["land_note"] = note
        q._write(deck, claim)
    return outcome

def land_worktree(wid):
    return os.path.join(ROOT, ".claude", "worktrees", "artifact-land-" + wid)


def suite_key(wt, deck_dir):
    """-> the regression-suite key (test/regression_cases.sh DECK_FILE) of the deck in `deck_dir`, or None."""
    txt = open(os.path.join(wt, "test", "regression_cases.sh")).read()
    m = re.search(r"declare -A DECK_FILE=\((.*?)\n\)", txt, re.S)
    for k, q, v in re.findall(r'\[([A-Za-z0-9_]+)\]=(?:"([^"]*)"|(\S+))', m.group(1) if m else ""):
        if os.path.dirname(q or v) == deck_dir.rstrip("/"):
            return k
    return None


def land_result(wid, cfg, branch, deck_dir, ledger_only=False):
    """ADOPTION STEP (USER 2026-10-26: "change the pipeline to include adoption steps after each successful
    improvement"). Lands one result branch on cfg["branch"] and returns (sha, note) or (None, why-not).

    The drivers' own A/Bs already decided adopt vs keep (the old-vs-new versus pool for a keep table, the
    leaf's cheaper-and-not-notably-worse gate), so a branch here is either a VERDICT ONLY (incumbent kept:
    only the ledger changed, no play moves, lands as-is) or an ADOPTION, which lands as the regression-testing
    flow lands any artifact: cherry-picked onto the branch TIP, rebuilt, the deck's own smoke and regression
    cases run, their GT accepted, check_gt_logs clean, pushed. The deck's overnight GT is left to the periodic
    overnight run (CLAUDE.md: the overnight tier is never per change).

    Anything that is not a clean, completed run stays on its branch for an agent, untouched: a cherry-pick
    conflict, a tier that aborts (scenario failure, crash, no `Result:` line), stale gt_logs, a lost push race.
    A GT MOVE IS EXPECTED -- that is what adopting an artifact does -- and is not a failure. No USER sign-off is
    owed for an adoption that is better overall (USER 2026-10-26): report the numbers as done. Only a trade-off
    (better quality but slower, or the reverse) is the USER's, and the drivers never adopt one."""
    lw = land_worktree(wid)
    br = cfg["branch"]
    git("fetch", "-q", "origin", br, branch)
    if not os.path.exists(os.path.join(lw, ".git")):
        os.makedirs(os.path.dirname(lw), exist_ok=True)
        git("worktree", "add", "--detach", lw, "origin/" + br)
    if git("status", "--porcelain", cwd=lw).strip():
        return None, "landing worktree %s is not clean -- left for an agent" % lw
    GT_PATHS = ["test/regression_gt.txt", "test/gt_logs", "test/job_cost.tsv"]
    tested = None                               # the tip the deck's tiers last ran on
    for attempt in (1, 2, 3):
        git("fetch", "-q", "origin", br, cwd=lw)
        git("checkout", "-q", "--", *GT_PATHS, cwd=lw)    # the harness's own writes from a lost attempt
        git("checkout", "-q", "--detach", "origin/" + br, cwd=lw)
        tip = git("rev-parse", "HEAD", cwd=lw).strip()
        r = subprocess.run(["git", "cherry-pick", "origin/" + branch], cwd=lw, capture_output=True, text=True)
        if r.returncode != 0:
            subprocess.run(["git", "cherry-pick", "--abort"], cwd=lw, capture_output=True)
            return None, "cherry-pick onto %s conflicts (%s) -- left for an agent" % (br, r.stderr.strip()[:200])
        changed = git("diff", "--name-only", "-z", "HEAD~1", "HEAD", cwd=lw).split("\0")
        changed = [f for f in changed if f]
        extra = [f for f in changed if not f.startswith("test/artifact_ledger/")]
        if ledger_only and extra:
            # A KEPT INCUMBENT lands its verdict and nothing else. Before 6e42ca948 the worker unpacked the
            # incumbent's raw sidecar onto the generator's checkpoint path, and re-packed the regeneration's raw
            # over it: Anti-Lifegain's "rejected" branch carries a NEW raw beside the OLD keep table. Drop it.
            for f in extra:
                if git("ls-tree", "HEAD~1", "--", f, cwd=lw).strip():
                    git("checkout", "HEAD~1", "--", f, cwd=lw)
                else:
                    git("rm", "-q", "--", f, cwd=lw)
            git("commit", "-q", "--amend", "-m", git("log", "-1", "--format=%B", cwd=lw).rstrip() +
                "\n\nLanded as VERDICT ONLY: dropped %s (a kept incumbent changes no artifact).\n" % ", ".join(extra), cwd=lw)
            changed = [f for f in changed if f not in extra]
        verdict_only = all(f.startswith("test/artifact_ledger/") for f in changed)
        if not verdict_only:
            key = suite_key(lw, deck_dir)
            if not key:
                return None, "%s is not in test/regression_cases.sh -- left for an agent" % deck_dir
            # `--deck=` matches case keys EXACTLY: a deck's 2HG cases are keyed "<k>2hg" (mullgen.sh does the same).
            if re.search(r'^\s*"\s*%s2hg\s' % re.escape(key), open(os.path.join(lw, "test", "regression_cases.sh")).read(), re.M):
                key = key + "," + key + "2hg"
            # A lost push race re-uses the tiers' results when the new tip cannot move this deck's play (no src/,
            # build, card-data or deck-folder change): --accept runs no binary, it merges those results into the
            # tip's GT -- never a hand merge of regression_gt.txt (CLAUDE.md). Anything else re-runs the tiers.
            rerun = not (tested and subprocess.run(
                ["git", "diff", "--quiet", tested, tip, "--", "src", "CMakeLists.txt", "cmake", "build.sh", deck_dir],
                cwd=lw).returncode == 0)
            if rerun:
                wait_idle(wid, cfg)
                if run_logged(wid, ["./build.sh"], lw, "land.build.log") != 0:
                    return None, "build failed in the landing worktree -- left for an agent"
            ran = []
            for mode in ("--smoke", "--regression"):
                args = ["bash", "test/regression.sh"] + ([mode] if mode == "--smoke" else []) + ["--deck=" + key]
                lp = os.path.join(state_dir(wid), "land.%s.%s.log" % (key, mode.strip("-")))
                if rerun:
                    with open(lp, "w") as f:
                        rc = subprocess.run(args, cwd=lw, stdout=f, stderr=subprocess.STDOUT).returncode
                else:
                    rc = 0
                out = open(lp).read() if os.path.exists(lp) else ""
                if "matched no" in out:
                    continue                    # the deck has no cases in this tier
                if not re.search(r"^Result: \d+ passed, \d+ failed", out, re.M):
                    return None, "%s %s did not complete (rc=%d; see %s) -- left for an agent" % (mode, key, rc, lp)
                if "gt_logs are STALE" in out:
                    return None, "%s: committed gt_logs are STALE -- the audit is unattributed; left for an agent" % mode
                res = re.search(r"^Result: .*$", out, re.M).group(0)
                acc = ["bash", "test/regression.sh"] + ([mode] if mode == "--smoke" else []) + ["--accept", "--deck=" + key]
                if run_logged(wid, acc, lw, "land.accept.log") != 0:
                    return None, "%s --accept failed -- left for an agent" % mode
                ran.append("%s: %s (log %s)" % (mode.strip("-"), res, lp))
            if rerun:
                tested = tip
            if subprocess.run(["python3", "test/check_gt_logs.py"], cwd=lw, capture_output=True).returncode != 0:
                return None, "check_gt_logs.py fails after the accept -- left for an agent"
            git("add", "-A", "--", *GT_PATHS, cwd=lw)
            if git("diff", "--cached", "--name-only", cwd=lw).strip():
                git("commit", "-q", "-m", "gt(%s): accept smoke + regression for the adopted artifact (%s)\n\n%s\n\n"
                    "Accepted by the artifact queue's adoption step (land_result); the deck's overnight GT is left to\n"
                    "the periodic overnight run.\n\nCo-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>\n"
                    % (key, branch, "\n".join("  " + x for x in ran)), cwd=lw)
        r = subprocess.run(["git", "push", "-q", "origin", "HEAD:" + br], cwd=lw, capture_output=True, text=True)
        if r.returncode == 0:
            sha = git("rev-parse", "HEAD", cwd=lw).strip()
            return sha, ("verdict only, landed as-is" if verdict_only else "adopted: " + "; ".join(ran))
        # Lost a race: the tip moved under us. Redo it once from the new tip (the tiers re-run, since the
        # new commits may move play); a second loss is left for an agent.
    return None, "push to %s lost the race three times -- left for an agent" % br


def cmd_land(a):
    """Land result branches by hand with the same adoption step (the backlog from before it existed)."""
    wid = worker_id()
    cfg = load_config()
    q = Queue()
    q.fetch()
    claims = q.claims()
    rc = 0
    for b in a.branches:
        b = b if b.startswith(RESULT_PREFIX) else RESULT_PREFIX + b
        c = next((c for c in claims.values() if c.get("result_branch") == b), None)
        if not c or c.get("state") not in ("done", "rejected"):
            print("%s: refused -- its claim is %s, not done/rejected (a released or failed run never lands)"
                  % (b, c.get("state") if c else "missing"))
            rc = 1
            continue
        if str(c.get("land_note", "")).startswith("HELD") and not a.force:
            print("%s: refused -- %s (the USER's call; pass --force once they have decided)" % (b, c["land_note"]))
            rc = 1
            continue
        d = next((x for x in shipping_decks(ROOT) if x.key == c["deck"]), None)
        if d is None:
            print("%s: refused -- deck %s is not a shipping deck here" % (b, c["deck"]))
            rc = 1
            continue
        sha, note = land_result(wid, cfg, b, d.dir, ledger_only=(c["state"] == "rejected"))
        log(wid, "LAND %s: %s%s" % (b, ("landed %s -- " % sha[:9]) if sha else "NOT landed -- ", note))
        print("%s: %s%s" % (b, ("landed %s -- " % sha[:9]) if sha else "NOT landed -- ", note))
        if sha:
            c.update({"landed": sha, "landed_at": iso(utcnow())})
            q._write(c["deck"], c)
        else:
            rc = 1
    return rc


def cmd_run(a):
    wid = worker_id()
    cfg = load_config()
    q = Queue()
    log(wid, "worker %s starting (pid %d)%s" % (wid, os.getpid(), " DRY RUN" if a.dry_run else ""))
    try:
        os.remove(os.path.join(state_dir(wid), "STOP"))
    except OSError:
        pass
    refuse_temp_root()
    fails = 0
    while not stop_requested(wid):
        inflight = os.path.join(state_dir(wid), "inflight.json")
        if os.path.exists(inflight):
            # Interrupted mid-job: resume in the SAME frozen worktree (both drivers resume from their own
            # markers/journals). Re-syncing here would re-freeze on a newer commit under a half-done run.
            st = json.load(open(inflight))
            wt, frozen = worktree_path(wid), st["claim"]["frozen"]
            job = st["job"]
            log(wid, "resuming in-flight %s at %s" % (job["deck"], frozen[:9]))
        else:
            wt, frozen = (ROOT, "HEAD") if a.dry_run else sync_worktree(wid, cfg)
            # Marks/config as landed on the branch. Until the tool itself has landed there, the frozen tree
            # has no config file and load_config() would return the bare defaults, silently dropping every
            # USER mark (2026-10-25: Angels/Giants marked p5 were passed over for three drift-ranked decks).
            cfg = load_config(wt if os.path.exists(os.path.join(wt, CONFIG_REL)) else ROOT)
            items, _ = assess_all(root=wt, cfg=cfg)
            jobs = deck_jobs(items, cfg)
            q.fetch()
            claims = q.claims()
            note_claim_drift(wt, frozen if frozen != "HEAD" else "HEAD", jobs, claims)
            now = utcnow()
            job = next((j for j in jobs if not claim_blocks(claims.get(j["deck"]), j, cfg, wid, now)), None)
            if job is None:
                log(wid, "nothing to do (%d deck job(s), all claimed or done); sleeping 1 h" % len(jobs))
                if a.once:
                    return 0
                for _ in range(12):
                    if stop_requested(wid):
                        break
                    time.sleep(300)
                continue
        d = next(x for x in shipping_decks(wt) if x.key == job["deck"])
        deckinfo = {"dir": d.dir, "stem": d.stem, "value": d.value, "profile": d.profile}
        if not os.path.exists(inflight) and not a.dry_run:
            if wait_idle(wid, cfg):
                continue                                # waited for the box: re-sync and re-rank first
            if stop_requested(wid):
                break
            q.fetch()
            if claim_blocks(q.claims().get(job["deck"]), job, cfg, wid, utcnow()):
                continue                                # lost the race to another machine
        try:
            outcome = run_job(wid, cfg, q, job, wt, frozen if frozen != "HEAD" else git("rev-parse", "HEAD").strip(),
                              deckinfo, a.dry_run)
        except MachineFault:
            return 1
        fails = fails + 1 if outcome == "failed" else 0
        if fails >= 3:
            log(wid, "3 deck jobs FAILED in a row -- stopping this worker: that is the box, not the decks. "
                     "See the per-deck logs in %s." % state_dir(wid))
            return 1
        if a.once or a.dry_run:
            return 0
    log(wid, "STOP requested -- exiting between jobs")
    return 0


def refuse_temp_root():
    """The worker's worktree, journals and in-flight state live under this checkout; a temp root loses them
    with the container (the /tmp references loss, CLAUDE.md). Same stance as tools/play/refsafe.js."""
    import tempfile
    r = os.path.realpath(ROOT)
    for t in {"/tmp", "/var/tmp", os.path.realpath(tempfile.gettempdir())}:
        if r == t or r.startswith(t.rstrip("/") + "/"):
            raise SystemExit("REFUSING: %s is under %s -- run the worker from a persistent checkout" % (ROOT, t))


def cmd_start(a):
    refuse_temp_root()
    wid = worker_id()
    lp = os.path.join(state_dir(wid), "worker.out")
    pidf = os.path.join(state_dir(wid), "worker.pid")
    if os.path.exists(pidf):
        try:
            pid = int(open(pidf).read())
            os.kill(pid, 0)
            print("worker %s already running (pid %d); log %s" % (wid, pid, lp))
            return 1
        except (OSError, ValueError):
            pass
    with open(lp, "a") as out:
        p = subprocess.Popen(["nohup", sys.executable, os.path.abspath(__file__), "run"], cwd=ROOT,
                             stdout=out, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, start_new_session=True)
    open(pidf, "w").write(str(p.pid))
    print("worker %s started (pid %d). log: %s   stop: %s stop" % (wid, p.pid, lp, sys.argv[0]))
    return 0


def cmd_stop(a):
    wid = worker_id()
    open(os.path.join(state_dir(wid), "STOP"), "w").write(iso(utcnow()))
    print("STOP requested for worker %s: it exits after the CURRENT job (never mid-generation)." % wid)
    return 0


def cmd_pause(a):
    """USER cancel of the CURRENT job, kept resumable (USER 2026-10-26: "The ideal is that next time we run the
    pipeline here, it starts up Hinata. But otherwise it is set aside."). `stop` waits for the job to end, which
    for a multi-day generation is no answer. This stops the worker NOW (SIGTERM to its process group: the worker,
    the driver and the generator; the journal is durable and its rolling backups are minutes old), leaves
    inflight.json in place so the next `start` on this checkout resumes the job in the same frozen worktree, and
    marks the claim `paused`, which no other worker takes over, however old its heartbeat."""
    wid = worker_id()
    sd = state_dir(wid)
    open(os.path.join(sd, "STOP"), "w").write(iso(utcnow()))
    pidf = os.path.join(sd, "worker.pid")
    pid = 0
    try:
        pid = int(open(pidf).read())
        os.kill(pid, 0)
    except (OSError, ValueError):
        pid = 0
    if pid:
        os.killpg(os.getpgid(pid), signal.SIGTERM)
        for _ in range(120):
            try:
                os.kill(pid, 0)
                time.sleep(1)
            except OSError:
                break
        else:
            print("worker pid %d did not exit within 120 s of SIGTERM -- check it by hand" % pid)
            return 1
        log(wid, "PAUSED by the USER: worker stopped mid-job (pid %d)" % pid)
    ifp = os.path.join(sd, "inflight.json")
    if not os.path.exists(ifp):
        print("worker %s stopped; no job was in flight" % wid)
        return 0
    claim = json.load(open(ifp))["claim"]
    claim.update({"state": "paused", "paused": iso(utcnow()),
                  "detail": "paused by the USER: resumes when worker %s next starts; set aside for every other "
                            "worker" % wid})
    q = Queue()
    q.fetch()
    if not put_claim(q, claim["deck"], claim, wid):
        print("could not write the paused claim for %s (another worker holds it?)" % claim["deck"])
        return 1
    print("paused %s (stage %s). `start` on this checkout resumes it; no other worker will take it."
          % (claim["deck"], claim.get("stage")))
    return 0


def cmd_selftest(a):
    """Everything that can be checked without a generation: the claim CAS against a throwaway local bare
    repo (no push to origin), the lease/landing rules, the .gz incumbent round-trip, decklist parsing,
    case-key matching and job ordering. Run it on a new machine before `start`."""
    import tempfile
    ok = [0, 0]

    def check(name, cond):
        ok[0 if cond else 1] += 1
        print("  %s  %s" % ("ok  " if cond else "FAIL", name))

    T = tempfile.mkdtemp(prefix="aq_selftest_")
    env_keys = ("GIT_AUTHOR_NAME", "GIT_COMMITTER_NAME", "GIT_AUTHOR_EMAIL", "GIT_COMMITTER_EMAIL")
    saved = {k: os.environ.get(k) for k in env_keys}
    os.environ.update({"GIT_AUTHOR_NAME": "selftest", "GIT_COMMITTER_NAME": "selftest",
                       "GIT_AUTHOR_EMAIL": "selftest@localhost", "GIT_COMMITTER_EMAIL": "selftest@localhost"})
    try:
        print("claim queue (local bare repo %s):" % T)
        git("init", "-q", "--bare", T + "/origin.git", cwd=T)
        for c in ("a", "b"):
            git("clone", "-q", T + "/origin.git", T + "/" + c, cwd=T)
        cfg = load_config()
        qa, qb = Queue(T + "/a"), Queue(T + "/b")
        job = {"deck": "snow", "blobs": {"mulligan": "x"}}
        mk = lambda w, st="running", **kw: dict({"deck": "snow", "worker": w, "state": st, "started": iso(utcnow()),
                                                 "blobs": job["blobs"]}, **kw)
        check("A claims a free deck", put_claim(qa, "snow", mk("A"), "A", job, cfg))
        qb.fetch()
        check("B sees A's live claim as blocking", claim_blocks(qb.claims().get("snow"), job, cfg, "B", utcnow()))
        check("B's claim of the same deck is refused", not put_claim(qb, "snow", mk("B"), "B", job, cfg))
        qa._write("hinata", {"deck": "hinata", "worker": "A", "state": "running", "started": iso(utcnow())})
        qb._write("burn", {"deck": "burn", "worker": "B", "state": "running", "started": iso(utcnow())})
        qa.fetch()
        check("independent decks coexist", sorted(qa.claims()) == ["burn", "hinata", "snow"])
        old = iso(utcnow() - datetime.timedelta(hours=cfg["lease_hours"] + 1))
        qa._write("snow", mk("A", heartbeat=old, started=old))
        check("B takes over an EXPIRED lease", put_claim(qb, "snow", mk("B"), "B", job, cfg))
        qb._write("snow", mk("B", "done", finished=iso(utcnow()), result_branch=RESULT_PREFIX + "snow-x"))
        qa.fetch()
        c = qa.claims()["snow"]
        check("a done claim with a result to land holds", claim_blocks(c, job, cfg, "A", utcnow()))
        check("...and releases once the artifacts change",
              not claim_blocks(c, {"deck": "snow", "blobs": {"mulligan": "y"}}, cfg, "A", utcnow()))
        c2 = dict(c, result_branch=None, frozen="f0")
        late = utcnow() + datetime.timedelta(days=cfg["retry_rejected_days"] + 30)
        for st in ("done", "rejected", "refused"):
            c3 = dict(c2, state=st)
            check("a %s claim with nothing to land HOLDS on unmeasured drift, however old" % st,
                  claim_blocks(c3, job, cfg, "A", late))
            check("...holds while drift since its frozen commit is below min_drift",
                  claim_blocks(c3, dict(job, drift_since={"f0": cfg["min_drift"] - 0.001}), cfg, "A", late))
            check("...and is retryable once drift reaches it",
                  not claim_blocks(c3, dict(job, drift_since={"f0": cfg["min_drift"]}), cfg, "A", utcnow()))
            check("...drift measured from a DIFFERENT frozen commit does not count",
                  claim_blocks(c3, dict(job, drift_since={"f1": 0.5}), cfg, "A", late))
        check("a failed claim holds inside the retry window", claim_blocks(dict(c2, state="failed"), job, cfg, "A", utcnow()))
        check("...and is retryable after it",
              not claim_blocks(dict(c2, state="failed"), job, cfg, "A",
                               utcnow() + datetime.timedelta(days=cfg["retry_rejected_days"] + 1)))
        check("a released claim never blocks", not claim_blocks(dict(c, state="released"), job, cfg, "A", utcnow()))
        check("a paused claim blocks every other worker, however stale",
              claim_blocks(dict(c, state="paused", worker="A", heartbeat="2000-01-01T00:00:00Z"), job, cfg, "B", utcnow()))
        check("...but not the worker that paused it", not claim_blocks(dict(c, state="paused", worker="A"), job, cfg, "A", utcnow()))

        print(".gz incumbent round-trip:")
        W = T + "/wt"
        os.makedirs(W + "/decks/X")
        git("init", "-q", cwd=W)
        for part, body in (("profile", b'{"a":1}'), ("raw", b'{"m":1}')):
            with gzip.GzipFile("%s/decks/X/X.keepmodel.exhaustive.%s.json.gz" % (W, part), "wb") as f:
                f.write(body)
        git("add", "-A", cwd=W)
        git("commit", "-qm", "i", cwd=W)
        d = {"dir": "decks/X", "stem": "X"}
        ungzip_incumbent(W, d)
        check("incumbent PROFILE unpacked to the plain name the driver tests for; the raw stays .gz "
              "(a plain raw is the generator's floor checkpoint and would be resumed)",
              sorted(os.listdir(W + "/decks/X")) == ["X.keepmodel.exhaustive.profile.json", "X.keepmodel.exhaustive.raw.json.gz"])
        regzip(W, d)
        check("unchanged -> original .gz restored byte-for-byte", not git("status", "--porcelain", cwd=W).strip())
        ungzip_incumbent(W, d)
        open(W + "/decks/X/X.keepmodel.exhaustive.profile.json", "w").write('{"a":2}')
        regzip(W, d)
        check("changed -> re-packed .gz carries the new table",
              gzip.open(W + "/decks/X/X.keepmodel.exhaustive.profile.json.gz").read() == b'{"a":2}'
              and not os.path.exists(W + "/decks/X/X.keepmodel.exhaustive.profile.json"))

        print("parsing + ranking:")
        open(T + "/l.txt", "w").write("# c\n4 Lightning Bolt\n20 Mountain\n\nSideboard\n2 Smash\n")
        check(".txt mainboard stops at Sideboard", main_cards(T + "/l.txt") == {"Lightning Bolt", "Mountain"})
        open(T + "/l.cod", "w").write('<cockatrice_deck><zone name="main"><card number="4" name="Skred"/></zone>'
                                      '<zone name="side"><card number="1" name="X"/></zone></cockatrice_deck>')
        check(".cod mainboard only", main_cards(T + "/l.cod") == {"Skred"})
        m1, m2 = WINS_RE.match("fungus_overnight_d3_s4004.wins"), WINS_RE.match("fungusv32hg_smoke_d3_s1001.wins")
        check("case keys do not bleed (fungus vs fungusv3 2HG)",
              m1.group("key") == "fungus" and m2.group("key") == "fungusv3" and m2.group("hg"))
        check(".wins lines parse", parse_wins(b"0 7 ab\n1 4 cd\n") == {0: (7, "ab"), 1: (4, "cd")})
        mkit = lambda deck, kind, dt, flags=(), mark=None: dict(
            {"deck": deck, "dir": "decks/" + deck, "kind": kind, "flags": list(flags), "blob": deck + kind,
             "churn": {"abs_dturn": dt}}, **({"mark": mark} if mark else {}))
        its = [mkit("a", "valueleaf", 0.9), mkit("a", "mulligan", 0.01), mkit("b", "mulligan", 0.3, [("decklist", "")]),
               mkit("c", "mulligan", 0.001, mark={"priority": 1}), mkit("d", "mulligan", 0.001)]
        for it in its:
            score(it, cfg)
        jobs = deck_jobs(sorted(its, key=lambda i: i["sort"]), cfg)
        check("USER mark first, then a decklist mismatch, then drift; fresh decks not queued",
              [j["deck"] for j in jobs] == ["c", "b", "a"])
        check("a deck's stages run mulligan before valueleaf",
              all(j["stages"] == [k for k in KINDS if k in j["stages"]] for j in jobs))

        print("regeneration recipe = the incumbent's, fast or complete only:")
        P = lambda R, d, b, sd, sb: mull_plan(dict(cfg, recipe_overrides={}), "x", R, d, b, (sd, sb))
        p1 = P(30, 1, 3, 1, 3)
        check("R30 incumbent -> fast, screened setting as-is", p1["recipe"] == "fast" and p1["depth"] is None)
        check("R22 incumbent -> fast (R30 >= 22)", P(22, None, None, 1, 3)["recipe"] == "fast")
        check("R40 incumbent -> complete", P(40, 1, 1, 1, 1)["recipe"] == "complete")
        p60 = P(60, None, None, 1, 3)
        check("R60 incumbent -> beyond complete: left be, no pin",
              p60["recipe"] == "complete" and p60["beyond"] and p60["env"] == {})
        p2 = P(40, 5, 20, 1, 3)
        check("incumbent d5 b20 vs screened d1 b3 -> screened as-is (depth not imposed)",
              (p2["depth"], p2["budget"], p2["env"]) == (None, None, {}))
        it = {"churn": {"abs_dturn": 1.0}, "flags": [], "kind": "mulligan", "left_be": True}
        score(it, cfg)
        check("a left-be table is not queued on drift alone", not it["queued"])
        it = dict(it, mark={"priority": 1}); score(it, cfg)
        check("...but a USER mark queues it", it["queued"])
        check("recipe_overrides still wins", mull_plan(dict(cfg, recipe_overrides={"x": "fast"}), "x", 60, None, None,
                                                        (1, 3))["recipe"] == "fast")

        print("verdict ledger:")
        L = T + "/ledger_root"
        append_ledger(L, "snow", {"run_id": "r1", "kind": "mulligan", "verdict": "kept-incumbent"})
        append_ledger(L, "snow", {"run_id": "r2", "kind": "valueleaf", "verdict": "adopted"})
        check("ledger appends and reads back", [e["run_id"] for e in read_ledger(L, "snow")] == ["r1", "r2"])
        kept = dict(c, state="rejected", run_id="r9", result_branch=RESULT_PREFIX + "snow-y", finished=iso(utcnow()))
        check("a kept-incumbent verdict waiting to land blocks a redo, even past the retry window",
              claim_blocks(kept, job, cfg, "A", utcnow() + datetime.timedelta(days=cfg["retry_rejected_days"] + 30)))
        check("...and stops blocking once it has landed (the ledger governs)",
              not claim_blocks(kept, dict(job, ledger_runs=["r9"]), cfg, "A", utcnow()))

        print("a USER mark is answered by a later run of its kind:")
        led = [{"run_id": "giants-w-20261025T145443", "kind": "mulligan", "verdict": "adopted"},
               {"run_id": "giants-w-20261025T145443", "kind": "valueleaf", "verdict": "kept-incumbent"}]
        check("a date-only mark is answered by a run the next day", mark_answer({"date": "2026-10-24"}, "mulligan", led) is led[0])
        check("...but not by a run on the mark's own day", mark_answer({"date": "2026-10-25"}, "mulligan", led) is None)
        check("a timestamped mark is answered by a run started after it",
              mark_answer({"set": "2026-10-25T14:00:00Z"}, "mulligan", led) is led[0])
        check("...not by one started before it", mark_answer({"set": "2026-10-25T15:00:00Z"}, "mulligan", led) is None)
        check("...and not by a run of the other kind", mark_answer({"date": "2026-10-24"}, "mulligan", led[1:]) is None)
        check("a trade held for the USER answers it",
              mark_answer({"date": "2026-10-24"}, "mulligan", [dict(led[0], verdict="trade")]) is not None)
        check("an unknown verdict does not", mark_answer({"date": "2026-10-24"}, "mulligan", [dict(led[0], verdict="x")]) is None)
        it = mkit("e", "mulligan", 0.0)
        score(it, cfg)
        check("an answered mark (no it['mark']) leaves a fresh artifact unqueued", not it["queued"])
    finally:
        shutil.rmtree(T, ignore_errors=True)
        for k, v in saved.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
    print("%d passed, %d FAILED" % tuple(ok))
    return 1 if ok[1] else 0



def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    s = sp.add_parser("status")
    s.add_argument("--json", action="store_true")
    s.add_argument("--all", action="store_true")
    s = sp.add_parser("explain")
    s.add_argument("deck")
    for name in ("mark", "unmark"):
        s = sp.add_parser(name)
        s.add_argument("deck")
        s.add_argument("kind")
        if name == "mark":
            s.add_argument("why")
            s.add_argument("--priority", type=int, default=0)
    sp.add_parser("claims")
    s = sp.add_parser("run")
    s.add_argument("--once", action="store_true", help="one deck job, then exit")
    s.add_argument("--dry-run", action="store_true", help="rank and pick, claim nothing, run nothing")
    sp.add_parser("start")
    sp.add_parser("stop")
    sp.add_parser("pause")
    s = sp.add_parser("land", help="land result branches with the adoption step")
    s.add_argument("branches", nargs="+")
    s.add_argument("--force", action="store_true", help="land a branch HELD for the USER (a trade) once they decided")
    sp.add_parser("selftest")
    a = ap.parse_args()
    fn = {"status": cmd_status, "explain": cmd_explain, "mark": cmd_mark, "unmark": cmd_mark, "claims": cmd_claims,
          "run": cmd_run, "start": cmd_start, "stop": cmd_stop, "pause": cmd_pause, "land": cmd_land, "selftest": cmd_selftest}[a.cmd]
    return fn(a)


if __name__ == "__main__":
    signal.signal(signal.SIGINT, signal.default_int_handler)
    sys.exit(main())

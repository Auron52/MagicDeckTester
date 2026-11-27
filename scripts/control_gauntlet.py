#!/usr/bin/env python3
"""Control-scoring mode, stage 1: the GAUNTLET drivers (docs/design/control-scoring-mode.md sections 7, 7.5, 8;
the harness interface is docs/design/control-inject-harness.md; the baseline format and invalidation keys are
docs/design/control-mode-stage2-report.md section 5).

  baseline make   <dir> [--games N] [--games-for k=N,..] [--decks k,..] [--exclude k,..]
                        [--max-wall-hours H --threads T] [--identity k,..]
                  ONE pooled manifest: every gauntlet deck, seeds SEED_BASE + gi (gi = 0..N-1), max_turns 15,
                  ctrl_record on, at the deck's shipped play setting, plus the inert CENSUS schedule (index-999
                  no-op effects that only log each round's candidate lists; verified byte-identical by the
                  `<key>__plain` twin jobs of the --identity decks).
  baseline ingest <dir> [--out logs/control-mode/baseline]
                  <dir>/batch.out + batch.err + *.wins -> one ctrl-baseline-v1 table per deck + summary.txt.
  baseline check  [--out ...]          which stored tables are stale (decklist / artifacts / HEAD:src)
  plan            [--what baseline|table] [--games N] [--threads T] [--baseline-dir D] [--decks ..]
                  core-hours before anything is launched (test/suite_cost.json, refined by test/job_cost.tsv
                  units and by the baseline's own measured cpu when it exists).
  table make      <dir> --baseline-dir D [--decks k,..] [--games N] [--rounds 2-5] [--kinds a,b]
                        [--no-clairvoyant] [--no-joint] [--life-screen]
                  the singles (+ clairvoyant index sweeps, + joint stacking probes) manifest; paired with the
                  stored baseline by seed and game_index.
  report          <dir> --baseline-dir D [--json out.json] [--rows out.jsonl]
                  S[e,t,g] (fixed and clairvoyant, se, hit rate, unwon share), the analytic skip check, the
                  section 8.2 interaction terms, life gain by deck class, and the state-feature rows.

The run itself is always ONE `mtg --batch` in the small-job lane, e.g.
  scripts/box_lane.sh --slots 2 -- build/Release/mtg --batch <dir>/manifest.json --threads 2 \\
      --game-log-dir <dir> [--resume] > <dir>/batch.out 2> <dir>/batch.err
"""
import argparse
import hashlib
import json
import math
import os
import re
import subprocess
import sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import control_score as cs  # noqa: E402

FORMAT = "ctrl-baseline-v1"
HARNESS = "ctrl-inject-v1"          # the grammar of docs/design/control-inject-harness.md section 4
SEED_BASE = 8100001                 # stage-2 report section 4: held out from every tier (777,000..2,000,000,
#                                     the 1001..30521 suite bases) and from the probe's 7,900,001
HORIZON = cs.MAP_HORIZON            # 15 (Q5): the mapping is built at the largest horizon any deck may use
UNWON = HORIZON + 1
NOOP = 999                          # an index at or past the candidate count is a logged no-op
# The census: one no-op per candidate class, every round. It logs `count` / `candidates` / `features` exactly
# where the table's effects will apply (board / hand: round start before untap; tap: after untap). Counters
# need none: [ctrl-game] rounds[].spells lists each round's counter candidates.
CENSUS = [("land", "destroy:1-15:land:%d" % NOOP), ("creature", "destroy:1-15:creature:%d" % NOOP),
          ("hand", "discard:1-15:card:%d" % NOOP), ("untapped_land", "tap:1-15:land:%d" % NOOP),
          ("untapped_creature", "tap:1-15:creature:%d" % NOOP)]
CENSUS_SPEC = ";".join(s for _, s in CENSUS)
PILOT_DECKS = ["burn", "whiteknights", "stompy", "dragons", "breaching"]   # the section 8 probe decks
ART_SKIP = (".raw.json.gz",)        # not read by play (generation input only)

# --------------------------------------------------------------------------------------------------------------
# The effect catalogue. fixed = the non-clairvoyant policy (Q3 (a); counters: the probe's "first spell of mana
# value >= 1", i.e. the Q8 threshold at zero); sweep = the per-index template of the clairvoyant arm (Q3 (b) /
# Q8: try every candidate, keep the one that delays most); count = where the candidate count comes from.
# --------------------------------------------------------------------------------------------------------------
KINDS = {
    "land":    {"fixed": "destroy:{T}:land:0",               "sweep": "destroy:{T}:land:{i}",   "count": "land"},
    "crt":     {"fixed": "destroy:{T}:creature:big",         "sweep": "destroy:{T}:creature:{i}", "count": "creature"},
    "sweep":   {"fixed": "destroy:{T}:creature:all"},
    "ctr":     {"fixed": "counter:{T}:mv>=1",                "sweep": "counter:{T}:{i}",        "count": "spells"},
    "ctrhand": {"fixed": "counter_to_hand:{T}:mv>=1",        "sweep": "counter_to_hand:{T}:{i}", "count": "spells"},
    "ctrtop":  {"fixed": "counter_to_top:{T}:mv>=1",         "sweep": "counter_to_top:{T}:{i}", "count": "spells"},
    "disc":    {"fixed": "discard:{T}:nonland:big",          "sweep": "discard:{T}:card:{i}",   "count": "hand",
                "dedupe_names": True},
    "tax":     {"fixed": "tax:{T}-15:1"},
    "tapland": {"fixed": "tap:{T}:land:0",                   "sweep": "tap:{T}:land:{i}",       "count": "untapped_land"},
    "tapcrt":  {"fixed": "tap:{T}:creature:big",             "sweep": "tap:{T}:creature:{i}",   "count": "untapped_creature"},
    "life3":   {"fixed": "life:{T}:3", "life": 3},
    "life5":   {"fixed": "life:{T}:5", "life": 5},
    "xu":      {"fixed": "exile_until:{T}:creature:big:ret={R}"},
    "xuall":   {"fixed": "exile_until:{T}:creature:all:ret={R}"},
    "skip":    {"fixed": "skip:{T}"},
}
XU_ROUNDS = 2                       # exile_until returns this many rounds later
# Joint stacking probes (section 8, widened). Each part is (kind, round), fixed policy.
JOINT = {
    "land2+3": [("land", 2), ("land", 3)], "land3+4": [("land", 3), ("land", 4)],
    "land2+3+4": [("land", 2), ("land", 3), ("land", 4)], "land3+4+5": [("land", 3), ("land", 4), ("land", 5)],
    "crt2+3": [("crt", 2), ("crt", 3)], "crt3+4": [("crt", 3), ("crt", 4)],
    "crt3+4+5": [("crt", 3), ("crt", 4), ("crt", 5)],
    "ctr2+3": [("ctr", 2), ("ctr", 3)], "ctr3+4": [("ctr", 3), ("ctr", 4)],
    "ctr3+4+5": [("ctr", 3), ("ctr", 4), ("ctr", 5)], "ctr3+3": [("ctr", 3), ("ctr", 3)],
    "disc2+3": [("disc", 2), ("disc", 3)], "disc2+3+4": [("disc", 2), ("disc", 3), ("disc", 4)],
    "life3+4": [("life3", 3), ("life3", 4)],
    "land3+crt4": [("land", 3), ("crt", 4)], "crt3+ctr4": [("crt", 3), ("ctr", 4)],
    "disc2+land3": [("disc", 2), ("land", 3)], "ctr3+land4": [("ctr", 3), ("land", 4)],
    "crt3+life4": [("crt", 3), ("life3", 4)], "tapland3+land4": [("tapland", 3), ("land", 4)],
}
# The section 8.2 rows: (label, joint, [parts subtracted]); an "increment" row is (label, (a, b, c)) meaning
# (c - b) - (b - a) with a, b, c arm names.
INTERACTIONS = [
    ("land: (3+4) - 3 - 4", "land3+4", ["land3", "land4"]),
    ("land: (2+3) - 2 - 3", "land2+3", ["land2", "land3"]),
    ("crt: (3+4) - 3 - 4", "crt3+4", ["crt3", "crt4"]),
    ("crt: (2+3) - 2 - 3", "crt2+3", ["crt2", "crt3"]),
    ("ctr: (3+4) - 3 - 4", "ctr3+4", ["ctr3", "ctr4"]),
    ("ctr: (2+3) - 2 - 3", "ctr2+3", ["ctr2", "ctr3"]),
    ("disc: (2+3) - 2 - 3", "disc2+3", ["disc2", "disc3"]),
    ("ctr: two in round 3 - 2 x one", "ctr3+3", ["ctr3", "ctr3"]),
    ("life3: (3+4) - 3 - 4", "life3+4", ["life3r3", "life3r4"]),
    ("land3 + crt4 - parts", "land3+crt4", ["land3", "crt4"]),
    ("crt3 + ctr4 - parts", "crt3+ctr4", ["crt3", "ctr4"]),
    ("disc2 + land3 - parts", "disc2+land3", ["disc2", "land3"]),
    ("ctr3 + land4 - parts", "ctr3+land4", ["ctr3", "land4"]),
    ("crt3 + life3@4 - parts", "crt3+life4", ["crt3", "life3r4"]),
    ("tapland3 + land4 - parts", "tapland3+land4", ["tapland3", "land4"]),
    ("sweep3 - crt3", "sweep3", ["crt3"]),
]
INCREMENTS = [
    ("land: 3rd increment - 2nd", ("land3", "land3+4", "land3+4+5")),
    ("crt: 3rd increment - 2nd", ("crt3", "crt3+4", "crt3+4+5")),
    ("ctr: 3rd increment - 2nd", ("ctr3", "ctr3+4", "ctr3+4+5")),
    ("disc: 3rd increment - 2nd", ("disc2", "disc2+3", "disc2+3+4")),
]
# Section 8.1, mean of the five probe decks (d3 b10, max_turns 12, 200 games): the reference the pilot is read
# against. The pilot runs at PLAY settings and max_turns 15, so the two differ by construction.
PROBE_MEAN5 = {"skip3": 1.02, "land2": 1.06, "land3": 0.84, "land4": 0.31, "land3+4": 1.60, "land3+4+5": 2.20,
               "crt2": 0.52, "crt3": 0.58, "crt4": 0.53, "crt3+4": 1.24, "crt3+4+5": 1.84, "sweep3": 1.26,
               "sweep4": 1.76, "ctr2": 0.54, "ctr3": 0.82, "ctr4": 0.52, "ctr3+4": 1.37, "ctr3+4+5": 1.78,
               "ctr3+3": 1.25, "disc2": 0.51, "disc3": 0.48, "disc4": 0.24, "disc2+3": 1.08, "disc2+3+4": 1.40}
PROBE_INTER5 = {"land: (3+4) - 3 - 4": 0.45, "crt: (3+4) - 3 - 4": 0.13, "ctr: (3+4) - 3 - 4": 0.03,
                "disc: (2+3) - 2 - 3": 0.09, "ctr: two in round 3 - 2 x one": -0.38,
                "land: 3rd increment - 2nd": -0.16, "crt: 3rd increment - 2nd": -0.06,
                "ctr: 3rd increment - 2nd": -0.14, "disc: 3rd increment - 2nd": -0.25, "sweep3 - crt3": 0.68}


# --------------------------------------------------------------------------------------------------------------
# The suite: which decks, at which play setting
# --------------------------------------------------------------------------------------------------------------

def load_suite(root=ROOT):
    """-> {key: {"deck": path, "profile": path, "budget": play budget_ms}} for every suite deck, from
    test/regression_cases.sh (the regression tier's depth-5 row = the shipped play setting: the profile's
    value_play block decides the depth, budget_ms is the row's budget)."""
    script = ('source test/regression_cases.sh; for k in "${!DECK_FILE[@]}"; do '
              'printf "F\\t%s\\t%s\\t%s\\n" "$k" "${DECK_FILE[$k]}" "${DECK_PROF[$k]}"; done; '
              'printf "C\\t%s\\n" "${REGRESSION_CASES[@]}"')
    out = subprocess.run(["bash", "-c", script], cwd=root, check=True, capture_output=True, text=True).stdout
    return parse_suite(out)


def parse_suite(text):
    files, budgets = {}, defaultdict(lambda: defaultdict(int))
    for line in text.splitlines():
        p = line.split("\t")
        if p[0] == "F" and len(p) == 4:
            files[p[1]] = {"deck": p[2], "profile": p[3]}
        elif p[0] == "C" and len(p) == 2:
            f = p[1].split()
            if len(f) == 5 and f[1] == "5" and int(f[4]) > 0:
                budgets[f[0]][int(f[4])] += int(f[3])
    out = {}
    for k, v in files.items():
        if k not in budgets:
            continue                 # no play-setting row: not measurable at play settings
        b = max(budgets[k].items(), key=lambda kv: (kv[1], -kv[0]))[0]
        out[k] = dict(v, budget=b)
    return out


def regression_seeds(text_or_none=None, root=ROOT):
    """-> {(case_key, depth, budget): [seed, ...]} of the regression tier (for the units-scaled cost)."""
    if text_or_none is None:
        text_or_none = subprocess.run(["bash", "-c", 'source test/regression_cases.sh; '
                                       'printf "%s\\n" "${REGRESSION_CASES[@]}"'], cwd=root, check=True,
                                      capture_output=True, text=True).stdout
    out = defaultdict(list)
    for line in text_or_none.splitlines():
        f = line.split()
        if len(f) == 5:
            out[(f[0], int(f[1]), int(f[4]))].append(int(f[2]))
    return out


def gauntlet(root=ROOT):
    """Every suite deck whose modes marker does not say goldfish: false (USER Q2: every goldfish deck)."""
    import deck_registry
    suite = load_suite(root)
    out = {}
    for k, v in sorted(suite.items()):
        if deck_registry.goldfish_enabled(os.path.join(root, os.path.dirname(v["deck"]))):
            out[k] = v
    return out


# --------------------------------------------------------------------------------------------------------------
# Stamps (invalidation keys)
# --------------------------------------------------------------------------------------------------------------

def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def src_tree(root=ROOT):
    tree = subprocess.run(["git", "-C", root, "rev-parse", "HEAD:src"], capture_output=True, text=True).stdout.strip()
    dirty = subprocess.run(["git", "-C", root, "status", "--porcelain", "--", "src"], capture_output=True,
                           text=True).stdout.strip() != ""
    return tree, dirty


def deck_stamp(entry, root=ROOT):
    """The deck-side keys: decklist hash and every play-relevant artifact beside it (profile, value sidecar,
    keep table, buckets, ...; the raw generation input excluded)."""
    deck = os.path.join(root, entry["deck"])
    d = os.path.dirname(deck)
    stem = os.path.splitext(os.path.basename(entry["profile"]))[0].replace(".profile", "")
    arts = {}
    for f in sorted(os.listdir(d)):
        p = os.path.join(d, f)
        if os.path.isfile(p) and f.startswith(stem + ".") and p != deck and not f.endswith(ART_SKIP):
            arts[f[len(stem):]] = sha256(p)
    return {"decklist_sha256": sha256(deck), "artifacts": arts}


def stale_reasons(table, entry, tree, root=ROOT):
    st = table.get("stamp", {})
    now = deck_stamp(entry, root)
    why = []
    if st.get("decklist_sha256") != now["decklist_sha256"]:
        why.append("decklist")
    if st.get("artifacts") != now["artifacts"]:
        why.append("artifacts")
    if st.get("src_tree") != tree:
        why.append("src_tree")
    if st.get("harness") != HARNESS:
        why.append("harness")
    return why


# --------------------------------------------------------------------------------------------------------------
# Parsers (batch outputs)
# --------------------------------------------------------------------------------------------------------------

JOB_RE = re.compile(r"^(\S+): played=(\d+) avg=(\S+) digest=([0-9a-f]+) ms=(\d+) units=(\d+) cpu=(\d+)")
PLAY_RE = re.compile(r"^\[play\] (\S+) depth=(\d+) budget=(\d+)ms")


def parse_wins(text):
    """`<gi> <wt> <digest>` lines -> {gi: (wt, digest)}."""
    out = {}
    for line in text.splitlines():
        p = line.split()
        if len(p) >= 2:
            out[int(p[0])] = (int(p[1]), p[2] if len(p) > 2 else "")
    return out


def read_wins(path):
    with open(path) as f:
        return parse_wins(f.read())


def kill_round(wt, horizon=HORIZON):
    """.wins turn -> the kill round, or None (unwon; -2 voided is treated as unwon, never as a kill)."""
    return wt if 0 < wt <= horizon else None


def parse_stdout(text):
    """The batch's per-job result lines -> {job: {played, avg, digest, ms, units, cpu}}."""
    out = {}
    for line in text.splitlines():
        m = JOB_RE.match(line.strip())
        if m:
            out[m.group(1)] = {"played": int(m.group(2)), "avg": float(m.group(3)), "digest": m.group(4),
                               "ms": int(m.group(5)), "units": int(m.group(6)), "cpu": int(m.group(7))}
    return out


def parse_stderr(lines):
    """-> (games {(job, gi): [ctrl-game]}, injects {job: [[ctrl-inject] ...]}, play {job: (depth, budget)})."""
    games, injects, play = {}, defaultdict(list), {}
    for line in lines:
        if line.startswith("[ctrl-game] "):
            r = json.loads(line[12:])
            games[(r["job"], r["gi"])] = r
        elif line.startswith("[ctrl-inject] "):
            r = json.loads(line[14:])
            injects[r["job"]].append(r)
        else:
            m = PLAY_RE.match(line)
            if m:
                play[m.group(1)] = (int(m.group(2)), int(m.group(3)))
    return games, dict(injects), play


def census_of(inject_recs):
    """Census lines of one job -> {gi: {class: {round: [candidate names]}}, "features": {round: {...}}}."""
    by_spec = {s: c for c, s in CENSUS}
    out = defaultdict(lambda: defaultdict(dict))
    for r in inject_recs:
        c = by_spec.get(r.get("effect"))
        if c is None:
            continue
        out[r["gi"]][c][r["round"]] = list(r.get("candidates") or [])
        if c == "land":
            out[r["gi"]]["features"][r["round"]] = r.get("features") or {}
    return out


def game_record(gi, seed_base, wins, ctrl_game, census):
    wt, digest = wins
    rounds = (ctrl_game or {}).get("rounds") or []
    seed = seed_base + gi
    cen = census or {}
    rec = {"gi": gi, "seed": seed, "otp": seed % 2 == 0, "k": kill_round(wt), "wt": wt, "digest": digest,
           "kill": (ctrl_game or {}).get("kill"), "through_life": (ctrl_game or {}).get("through_life"),
           "alt": (ctrl_game or {}).get("kill") == "alt",
           "start_life": (ctrl_game or {}).get("start_life", 20),
           "dmg": [x.get("dmg") for x in rounds], "spells": [x.get("spells") or [] for x in rounds],
           "poison": [x.get("poison", 0) for x in rounds]}
    nr = len(rounds)
    for c, _ in CENSUS:
        per = cen.get(c, {})
        rec.setdefault("cands", {})[c] = [per.get(r) for r in range(1, nr + 1)]
    rec["features"] = [cen.get("features", {}).get(r) for r in range(1, nr + 1)]
    return rec


def build_tables(meta, stdout_text, stderr_lines, wins_by_job, tree_dirty, now_stamp=None):
    """-> ({key: ctrl-baseline-v1 table}, identity {key: (n_same, n_compared)})."""
    games, injects, play = parse_stderr(stderr_lines)
    jobinfo = parse_stdout(stdout_text)
    tables, identity = {}, {}
    for key, d in meta["decks"].items():
        job = "%s__base" % key
        wins = wins_by_job.get(job)
        if wins is None:
            continue
        cen = census_of(injects.get(job, []))
        recs = [game_record(gi, meta["seed_base"], wins[gi], games.get((job, gi)), cen.get(gi))
                for gi in sorted(wins)]
        info = jobinfo.get(job, {})
        depth, budget = play.get(job, (None, d["budget"]))
        stamp = dict((now_stamp or {}).get(key, {}))
        stamp.update({"src_tree": meta["src_tree"], "src_dirty": tree_dirty, "harness": HARNESS,
                      "seed_base": meta["seed_base"], "n": len(recs), "max_turns": HORIZON, "depth": depth,
                      "budget_ms": budget, "play_digest": info.get("digest"), "census": CENSUS_SPEC})
        tables[key] = {"format": FORMAT, "deck": key, "deck_file": d["deck"], "profile": d["profile"],
                       "stamp": stamp, "cpu_ms": info.get("cpu"), "games": recs}
        twin = wins_by_job.get("%s__plain" % key)
        if twin is not None:
            same = sum(1 for gi in wins if gi in twin and twin[gi] == wins[gi])
            identity[key] = (same, sum(1 for gi in wins if gi in twin))
    return tables, identity


# --------------------------------------------------------------------------------------------------------------
# Statistics
# --------------------------------------------------------------------------------------------------------------

def mean_se(xs):
    xs = [x for x in xs if x is not None]
    n = len(xs)
    if n == 0:
        return (None, None, 0)
    m = sum(xs) / n
    if n < 2:
        return (m, None, n)
    sd = math.sqrt(sum((x - m) ** 2 for x in xs) / (n - 1))
    return (m, sd / math.sqrt(n), n)


def mean_over_decks(cells):
    """Equal-weight mean over decks of per-deck (mean, se) cells; se = sqrt(sum se^2) / D."""
    cells = [c for c in cells if c and c[0] is not None]
    if not cells:
        return (None, None, 0)
    m = sum(c[0] for c in cells) / len(cells)
    ses = [c[1] for c in cells if c[1] is not None]
    se = math.sqrt(sum(s * s for s in ses)) / len(cells) if len(ses) == len(cells) else None
    return (m, se, len(cells))


def game_delay(base_k, arm_k):
    """One paired game's delay in rounds: unwon is scored HORIZON + 1 (the table's convention; the lock form is
    the scoring library's business). An undisrupted game that never kills gets 0 (control_score.delay)."""
    return cs.delay(base_k, arm_k, censored="bound", map_horizon=HORIZON)


def stats_summary(xs):
    m, se, n = mean_se(xs)
    return {"mean": m, "se": se, "n": n}


def summary_baseline(table):
    ks = [g["k"] for g in table["games"]]
    sc = [k if k is not None else UNWON for k in ks]
    kinds = defaultdict(int)
    for g in table["games"]:
        kinds[g.get("kill") or "none"] += 1
    n = len(ks)
    return {"n": n, "mean_k": (sum(sc) / n) if n else None, "unwon": sum(1 for k in ks if k is None),
            "unwon12": sum(1 for k in ks if k is None or k > 12), "max_k": max([k for k in ks if k] or [0]),
            "kills": dict(kinds), "ms_per_game": (table.get("cpu_ms") or 0) / n if n else None}


# --------------------------------------------------------------------------------------------------------------
# The table's arms
# --------------------------------------------------------------------------------------------------------------

def arm_name(kind, t):
    """`land3`, `ctr4`; a kind ending in a digit gets an `r`: `life3r4` (3 life on round 4)."""
    return "%s%s%d" % (kind, "r" if kind[-1].isdigit() else "", t)


def spec_of(kind, t):
    return KINDS[kind]["fixed"].format(T=t, R=t + XU_ROUNDS)


def candidates_at(game, kind, t):
    """The candidate names a `kind` effect of round t meets in this game (identical to the base game up to
    then), or None if the census / record has no entry (the round was never reached)."""
    src = KINDS[kind].get("count")
    if src == "spells":
        sp = game.get("spells") or []
        return list(sp[t - 1]) if t - 1 < len(sp) else None
    c = (game.get("cands") or {}).get(src) or []
    return c[t - 1] if t - 1 < len(c) and c[t - 1] is not None else None


def reached(game, t):
    """A round-t effect applies only if the base game was not won before round t."""
    return game["k"] is None or game["k"] >= t


def life_zero(game, t, amount):
    """Section 5.6 screen: a gain of L on round t is (by the recorded line) delay 0 when the kill is not through
    life, or the recorded damage at the kill round already reaches start_life + L."""
    if not reached(game, t):
        return True
    if game["k"] is None:
        return False
    if game.get("kill") not in (None, "life"):
        return True
    dmg = game.get("dmg") or []
    d = dmg[game["k"] - 1] if game["k"] - 1 < len(dmg) else None
    return d is not None and d >= game.get("start_life", 20) + amount


def sweep_indices(game, kind, t):
    """Indices the clairvoyant sweep must run for this game (name-deduped for hand cards: two copies of one
    card in hand are the same discard)."""
    names = candidates_at(game, kind, t)
    if not names or not reached(game, t):
        return []
    if KINDS[kind].get("dedupe_names"):
        seen, out = set(), []
        for i, n in enumerate(names):
            if n not in seen:
                seen.add(n)
                out.append(i)
        return out
    return list(range(len(names)))


def plan_arms(table, kinds, rounds, clairvoyant=True, joint=True, life_screen=False, games=None):
    """-> [{"arm", "kind", "t", "policy", "index", "spec", "run": [gi], "zero": [gi]}] for one deck. "zero" are
    games whose delay is known to be 0 without playing them (round not reached; the life screen)."""
    gl = [g for g in table["games"] if games is None or g["gi"] < games]
    arms = []
    for kind in kinds:
        for t in rounds:
            run, zero = [], []
            for g in gl:
                z = not reached(g, t)
                if not z and life_screen and "life" in KINDS[kind]:
                    z = life_zero(g, t, KINDS[kind]["life"])
                (zero if z else run).append(g["gi"])
            arms.append({"arm": arm_name(kind, t), "kind": kind, "t": t, "policy": "fixed", "index": None,
                         "spec": spec_of(kind, t), "run": run, "zero": zero})
            if clairvoyant and "sweep" in KINDS[kind]:
                per = {g["gi"]: sweep_indices(g, kind, t) for g in gl}
                top = max([max(v) + 1 for v in per.values() if v] or [0])
                for i in range(top):
                    run = [gi for gi in sorted(per) if i in per[gi]]
                    if run:
                        arms.append({"arm": "%s_i%d" % (arm_name(kind, t), i), "kind": kind, "t": t, "policy": "clair",
                                     "index": i, "spec": KINDS[kind]["sweep"].format(T=t, i=i), "run": run,
                                     "zero": []})
    if joint:
        for name, parts in JOINT.items():
            if not all(k in kinds and t in rounds for k, t in parts):
                continue
            t0 = min(t for _, t in parts)
            run = [g["gi"] for g in gl if reached(g, t0)]
            zero = [g["gi"] for g in gl if not reached(g, t0)]
            arms.append({"arm": name, "kind": "joint", "t": t0, "policy": "joint", "index": None,
                         "spec": ";".join(spec_of(k, t) for k, t in parts), "run": run, "zero": zero,
                         "parts": [arm_name(k, t) for k, t in parts]})
    return arms


def arm_job(key, table, arm, entry):
    gis = sorted(arm["run"])
    lo, hi = gis[0], gis[-1]
    skip = sorted(set(range(lo, hi + 1)) - set(gis))
    j = {"name": "%s__%s" % (key, arm["arm"]), "deck": entry["deck"], "profile": entry["profile"],
         "games": hi - lo + 1, "seed": table["stamp"]["seed_base"] + lo, "game_index": lo,
         "budget_ms": table["stamp"]["budget_ms"], "max_turns": HORIZON, "ctrl_inject": arm["spec"]}
    if skip:
        j["skip_games"] = skip
    return j


# --------------------------------------------------------------------------------------------------------------
# The report's math
# --------------------------------------------------------------------------------------------------------------

def arm_delays(table, arm, wins):
    """-> {gi: delay} over EVERY base game of the arm (run games from .wins, zero games 0). A run game missing
    from .wins is left out (an unfinished run), never counted 0."""
    base = {g["gi"]: g["k"] for g in table["games"]}
    out = {gi: 0.0 for gi in arm["zero"]}
    for gi in arm["run"]:
        if gi in wins:
            out[gi] = game_delay(base[gi], kill_round(wins[gi][0]))
    return out


def clairvoyant(table, kind, t, sweep_delays, fixed=None):
    """Per game: max over the swept indices' delays (Q3 / Q8); a game with no candidate, or not reached, is 0.
    `fixed` (the fixed policy's per-game delays) is folded in: its target is always one of the candidates (index 0,
    `big`, the first spell of mana value >= 1 all pick from the same list), so the max stays exact."""
    out = {}
    for g in table["games"]:
        idx = sweep_indices(g, kind, t)
        vals = [sweep_delays[i][g["gi"]] for i in idx if i in sweep_delays and g["gi"] in sweep_delays[i]]
        if fixed is not None and g["gi"] in fixed:
            vals.append(fixed[g["gi"]])
        if not idx:
            out[g["gi"]] = 0.0
        elif vals:
            out[g["gi"]] = max(vals)
    return out


def paired_combo(cells, sign):
    """sum_i sign_i * cells_i per game, over the games present in every cell."""
    common = set.intersection(*[set(c) for c in cells]) if cells else set()
    return [sum(s * c[gi] for s, c in zip(sign, cells)) for gi in sorted(common)]


def interaction_rows(delays):
    """delays: {arm: {gi: delay}} for one deck -> {label: (mean, se, n)} (section 8.2)."""
    out = {}
    for label, joint, parts in INTERACTIONS:
        if joint in delays and all(p in delays for p in parts):
            out[label] = mean_se(paired_combo([delays[joint]] + [delays[p] for p in parts],
                                              [1] + [-1] * len(parts)))
    for label, (a, b, c) in INCREMENTS:
        if all(x in delays for x in (a, b, c)):
            out[label] = mean_se(paired_combo([delays[c], delays[b], delays[a]], [1, -2, 1]))
    return out


def skip_check(table, t, wins):
    """Analytic check (harness doc section 7): skip:t gives +1 for every game with K >= t (unwon stays unwon)
    and +0 for every game won before t. -> (exact, total, [exceptions (gi, base_k, arm_k)])."""
    ok, tot, bad = 0, 0, []
    for g in table["games"]:
        if not reached(g, t):
            continue                 # identical by construction (not run)
        if g["gi"] not in wins:
            continue
        ak = kill_round(wins[g["gi"]][0])
        bk = g["k"]
        want = None if bk is None or bk + 1 > HORIZON else bk + 1
        tot += 1
        if ak == want:
            ok += 1
        else:
            bad.append((g["gi"], bk, ak))
    return ok, tot, bad


def deck_class(entry, table=None, cards=None, root=ROOT):
    """'combo' when most baseline kills ignore life (poison / alt / decked), else by the creature share of the
    nonland main deck: < 0.40 'burn', >= 0.55 'creature', between 'mixed'. A heuristic for the report's split
    only (USER 5.6: life gain is strong vs burn, weak vs creature decks); printed so it can be corrected."""
    if table is not None:
        ks = [g for g in table["games"] if g["k"] is not None]
        if ks and sum(1 for g in ks if g.get("kill") != "life") > len(ks) / 2:
            return "combo", None
    if cards is None:
        with open(os.path.join(root, "src/cards/data/cards.json")) as f:
            cards = {c["name"]: c for c in json.load(f)["cards"]}
    from analyze_deck import LoadDeckCounts
    from pathlib import Path
    counts = LoadDeckCounts(Path(os.path.join(root, entry["deck"])), board="main")
    cre = non = 0
    for n, c in counts.items():
        types = (cards.get(n) or {}).get("types") or []
        if "Land" in types:
            continue
        non += c
        cre += c if "Creature" in types else 0
    share = cre / non if non else 0.0
    return ("burn" if share < 0.40 else "creature" if share >= 0.55 else "mixed"), share


def race_flip(table, delays, w):
    """Share of the deck's games where a control deck killing on round w (on the play) wins the race only
    because of the effect: K < w <= K + D (the effect moved the gauntlet's kill to w or later)."""
    base = {g["gi"]: g["k"] for g in table["games"]}
    n = flip = 0
    for gi, d in delays.items():
        k = base.get(gi)
        if k is None:
            continue
        n += 1
        if k < w <= k + d:
            flip += 1
    return flip / n if n else None


# --------------------------------------------------------------------------------------------------------------
# Cost model
# --------------------------------------------------------------------------------------------------------------

def load_cost(root=ROOT):
    with open(os.path.join(root, "test/suite_cost.json")) as f:
        sc = json.load(f)["decks"]
    units = {}
    try:
        with open(os.path.join(root, "test/job_cost.tsv")) as f:
            for line in f:
                p = line.split()
                if len(p) >= 2 and not line.startswith("#"):
                    units[p[0]] = int(p[1])
    except FileNotFoundError:
        pass
    return sc, units


def play_cost_ms(key, budget, sc, units, seeds):
    """-> (upper bound, estimate) ms per game at the play setting. The upper bound is suite_cost's worst shape;
    the estimate scales it by the job_cost.tsv work units of the play rows over those of the worst row."""
    c = sc.get(key)
    if not c:
        return None, None
    hi = c["cost_per_game_ms"]
    m = re.match(r"^(.+)_regression_d(\d+)_b(\d+)$", c.get("worst_case", ""))
    if not m:
        return hi, hi

    def mean_units(case, d, b):
        xs = [units.get("%s_regression_d%d_s%d" % (case, d, s)) for s in seeds.get((case, d, b), [])]
        xs = [x for x in xs if x]
        return sum(xs) / len(xs) if xs else None
    uw = mean_units(m.group(1), int(m.group(2)), int(m.group(3)))
    up = mean_units(key, 5, budget)
    if not uw or not up:
        return hi, hi
    return hi, hi * min(1.0, up / uw)


# --------------------------------------------------------------------------------------------------------------
# Commands
# --------------------------------------------------------------------------------------------------------------

def _csv(s):
    return [x for x in (s or "").split(",") if x]


def _kv(s):
    out = {}
    for x in _csv(s):
        k, v = x.split("=")
        out[k] = int(v)
    return out


def cmd_plan(a):
    g = gauntlet()
    sc, units = load_cost()
    seeds = regression_seeds()
    decks = _csv(a.decks) or list(g)
    rows = []
    for k in decks:
        hi, est = play_cost_ms(k, g[k]["budget"], sc, units, seeds)
        meas = None
        bt = os.path.join(a.baseline_dir, k + ".json") if a.baseline_dir else None
        table = None
        if bt and os.path.exists(bt):
            with open(bt) as f:
                table = json.load(f)
            s = summary_baseline(table)
            meas = s["ms_per_game"]
        per = meas if meas else (est if a.estimate else hi)
        if a.what == "baseline":
            games = a.games
            factor = a.horizon_factor
        else:
            if table is not None:
                arms = plan_arms(table, list(KINDS), _rounds(a.rounds), not a.no_clairvoyant, not a.no_joint,
                                 a.life_screen, a.games)
                games = sum(len(x["run"]) for x in arms)
            else:
                games = a.games * a.arms_per_game
            factor = a.arm_factor
        rows.append((k, per, games, games * per * factor / 1000.0, "measured" if meas else
                     ("units-est" if a.estimate else "suite-max")))
    rows.sort(key=lambda r: -r[3])
    tot = sum(r[3] for r in rows)
    print("plan: %s, %d decks, threads %d (cost source per deck in the last column)" % (a.what, len(rows), a.threads))
    print("%-16s %10s %8s %10s  %s" % ("deck", "ms/game", "games", "core-s", "source"))
    for r in rows:
        print("%-16s %10.0f %8d %10.0f  %s" % (r[0], r[1], r[2], r[3], r[4]))
    print("TOTAL %.2f core-hours = %.2f h wall at %d threads" % (tot / 3600, tot / 3600 / a.threads, a.threads))


def _rounds(s):
    a, _, b = s.partition("-")
    return list(range(int(a), int(b or a) + 1))


def cmd_baseline_make(a):
    g = gauntlet()
    decks = _csv(a.decks) or list(g)
    decks = [k for k in decks if k not in set(_csv(a.exclude))]
    for k in decks:
        if k not in g:
            raise SystemExit("not a gauntlet deck: %s" % k)
    per_games = {k: a.games for k in decks}
    per_games.update({k: v for k, v in _kv(a.games_for).items() if k in per_games})
    dropped = []
    if a.max_wall_hours:
        sc, units = load_cost()
        seeds = regression_seeds()
        cost = {k: (play_cost_ms(k, g[k]["budget"], sc, units, seeds)[0] or 0) * per_games[k] * a.horizon_factor
                for k in decks}
        while decks and sum(cost[k] for k in decks) / 1000 / 3600 / a.threads > a.max_wall_hours:
            worst = max(decks, key=lambda k: cost[k])
            decks.remove(worst)
            dropped.append(worst)
    tree, dirty = src_tree()
    os.makedirs(a.dir, exist_ok=True)
    jobs = []
    sc, units = load_cost()
    seeds = regression_seeds()
    for k in decks:
        e = g[k]
        ms = play_cost_ms(k, e["budget"], sc, units, seeds)[0] or 0
        # LPT weight = projected core-ms, so the expensive decks start first (scheduling only, never a result).
        base = {"deck": e["deck"], "profile": e["profile"], "games": per_games[k], "seed": SEED_BASE,
                "game_index": 0, "budget_ms": e["budget"], "max_turns": HORIZON, "ctrl_record": True,
                "weight": int(ms * per_games[k])}
        jobs.append(dict(base, name="%s__base" % k, ctrl_inject=CENSUS_SPEC))
        if k in _csv(a.identity):
            jobs.append(dict(base, name="%s__plain" % k))
    meta = {"format": "ctrl-baseline-run-v1", "seed_base": SEED_BASE, "src_tree": tree, "src_dirty": dirty,
            "decks": {k: dict(g[k], games=per_games[k]) for k in decks}, "dropped": dropped,
            "stamps": {k: deck_stamp(g[k]) for k in decks}}
    with open(os.path.join(a.dir, "manifest.json"), "w") as f:
        json.dump({"jobs": jobs}, f, indent=1)
    with open(os.path.join(a.dir, "meta.json"), "w") as f:
        json.dump(meta, f, indent=1)
    print("%d jobs, %d decks, %d games -> %s/manifest.json" % (len(jobs), len(decks),
                                                             sum(j["games"] for j in jobs), a.dir))
    if dropped:
        print("DROPPED (over --max-wall-hours %.1f at %d threads), most expensive first: %s"
              % (a.max_wall_hours, a.threads, ", ".join(dropped)))
    if dirty:
        print("WARNING: src/ is dirty; the stamp's src_tree does not describe the binary")


def _read_run(d):
    with open(os.path.join(d, "meta.json")) as f:
        meta = json.load(f)
    with open(os.path.join(d, "batch.out"), errors="replace") as f:
        out = f.read()
    with open(os.path.join(d, "batch.err"), errors="replace") as f:
        err = f.read().splitlines()
    wins = {}
    for fn in os.listdir(d):
        if fn.endswith(".wins"):
            wins[fn[:-5]] = read_wins(os.path.join(d, fn))
    return meta, out, err, wins


def cmd_baseline_ingest(a):
    meta, out, err, wins = _read_run(a.dir)
    tables, identity = build_tables(meta, out, err, wins, meta.get("src_dirty"), meta.get("stamps"))
    os.makedirs(a.out, exist_ok=True)
    lines = ["control gauntlet baseline: %d decks, seeds %d + gi, max_turns %d (unwon = %d), src %s%s"
             % (len(tables), meta["seed_base"], HORIZON, UNWON, meta["src_tree"][:12],
                " (DIRTY)" if meta.get("src_dirty") else "")]
    lines.append("%-16s %4s %6s %6s %6s %5s %8s  %s" % ("deck", "n", "meanK", "unwon", ">12", "maxK", "ms/game",
                                                        "kills / depth b"))
    allk = []
    for k in sorted(tables):
        t = tables[k]
        with open(os.path.join(a.out, k + ".json"), "w") as f:
            json.dump(t, f, separators=(",", ":"))
        s = summary_baseline(t)
        allk.append(s["mean_k"])
        lines.append("%-16s %4d %6.2f %6d %6d %5d %8.0f  %s / d%s b%s" % (
            k, s["n"], s["mean_k"], s["unwon"], s["unwon12"], s["max_k"], s["ms_per_game"] or 0,
            ",".join("%s=%d" % kv for kv in sorted(s["kills"].items())), t["stamp"]["depth"], t["stamp"]["budget_ms"]))
    missing = sorted(set(meta["decks"]) - set(tables))
    lines.append("equal-weight mean kill round over %d decks: %.3f (unwon = %d)" % (len(allk), sum(allk) / len(allk),
                                                                                 UNWON) if allk else "no decks")
    if missing:
        lines.append("MISSING (no .wins): %s" % ", ".join(missing))
    if meta.get("dropped"):
        lines.append("DROPPED from this run (cost): %s" % ", ".join(meta["dropped"]))
    for k, (same, n) in sorted(identity.items()):
        lines.append("identity %s: census+record vs plain, %d / %d games byte-identical %s"
                     % (k, same, n, "PASS" if same == n else "FAIL"))
    txt = "\n".join(lines) + "\n"
    with open(os.path.join(a.out, "summary.txt"), "w") as f:
        f.write(txt)
    print(txt, end="")


def cmd_baseline_check(a):
    g = gauntlet()
    tree, dirty = src_tree()
    for fn in sorted(os.listdir(a.out)):
        if not fn.endswith(".json") or fn == "summary.json":
            continue
        with open(os.path.join(a.out, fn)) as f:
            t = json.load(f)
        if t.get("format") != FORMAT:
            continue
        k = t["deck"]
        why = stale_reasons(t, g[k], tree) if k in g else ["not in the gauntlet"]
        print("%-16s %s" % (k, "fresh" if not why else "STALE: " + ", ".join(why)))
    print("gauntlet decks with no table: %s" % (", ".join(sorted(set(g) - {f[:-5] for f in os.listdir(a.out)})) or "none"))


def load_tables(d, decks=None):
    out = {}
    for fn in sorted(os.listdir(d)):
        if fn.endswith(".json"):
            with open(os.path.join(d, fn)) as f:
                t = json.load(f)
            if t.get("format") == FORMAT and (not decks or t["deck"] in decks):
                out[t["deck"]] = t
    return out


def cmd_table_make(a):
    g = gauntlet()
    tables = load_tables(a.baseline_dir, _csv(a.decks))
    kinds = _csv(a.kinds) or list(KINDS)
    rounds = _rounds(a.rounds)
    tree, dirty = src_tree()
    jobs, arms_meta = [], {}
    for k, t in sorted(tables.items()):
        why = stale_reasons(t, g[k], tree)
        if why and not a.allow_stale:
            raise SystemExit("baseline of %s is stale (%s); rebuild it or pass --allow-stale" % (k, ", ".join(why)))
        arms = plan_arms(t, kinds, rounds, not a.no_clairvoyant, not a.no_joint, a.life_screen, a.games)
        arms_meta[k] = arms
        for arm in arms:
            if arm["run"]:
                jobs.append(arm_job(k, t, arm, g[k]))
    os.makedirs(a.dir, exist_ok=True)
    with open(os.path.join(a.dir, "manifest.json"), "w") as f:
        json.dump({"jobs": jobs}, f, indent=1)
    with open(os.path.join(a.dir, "meta.json"), "w") as f:
        json.dump({"format": "ctrl-table-run-v1", "src_tree": tree, "src_dirty": dirty, "rounds": rounds,
                   "kinds": kinds, "life_screen": a.life_screen, "games": a.games,
                   "baseline_dir": os.path.abspath(a.baseline_dir), "arms": arms_meta}, f)
    n = sum(len(x["run"]) for v in arms_meta.values() for x in v)
    print("%d jobs, %d decks, %d games to play -> %s/manifest.json" % (len(jobs), len(tables), n, a.dir))


def _fmt(c, w=6):
    m, se = c[0], c[1]
    if m is None:
        return "-".rjust(w + 6)
    return ("%+.2f" % m).rjust(w) + (("(%.2f)" % se) if se is not None else "(  - )")


def build_report(meta, tables, wins, injects):
    """-> the report dict (also what --json writes)."""
    rep = {"decks": {}, "mean": {}, "interactions": {}, "skip": {}, "life": {}, "hit": {}}
    rows = []
    for k, arms in meta["arms"].items():
        t = tables[k]
        delays = {}
        sweeps = defaultdict(dict)
        hit = {}
        unwon = {}
        by_job = defaultdict(dict)
        for r in injects.get(k, []):
            if r.get("n") == 0 and r.get("phase") not in ("end", "return"):
                by_job[r["job"]].setdefault(r["gi"], r)
        for arm in arms:
            job = "%s__%s" % (k, arm["arm"])
            w = wins.get(job, {})
            d = arm_delays(t, arm, w)
            if arm["policy"] == "clair":
                sweeps[(arm["kind"], arm["t"])][arm["index"]] = d
            else:
                delays[arm["arm"]] = d
                apps = by_job.get(job, {})
                hit[arm["arm"]] = (sum(1 for gi in arm["run"] if apps.get(gi, {}).get("applied", 0) > 0),
                                   len(arm["run"]))
                unwon[arm["arm"]] = sum(1 for gi in arm["run"] if gi in w and kill_round(w[gi][0]) is None)
            for gi, rec in by_job.get(job, {}).items():
                if gi in d:
                    rows.append({"deck": k, "arm": arm["arm"], "kind": arm["kind"], "t": arm["t"],
                                 "policy": arm["policy"], "index": arm["index"], "gi": gi, "delay": d[gi],
                                 "applied": rec.get("applied"), "count": rec.get("count"),
                                 "what": rec.get("what"), "features": rec.get("features")})
        cl = {}
        # Every swept kind and round, also when no game had a candidate (no index arm at all): delay 0 then.
        swept = {(x["kind"], x["t"]) for x in arms if x["policy"] == "fixed" and "sweep" in KINDS.get(x["kind"], {})}
        for (kind, tt) in sorted(swept | set(sweeps)):
            cl[arm_name(kind, tt)] = clairvoyant(t, kind, tt, sweeps.get((kind, tt), {}), delays.get(arm_name(kind, tt)))
        n = len(t["games"])
        rep["decks"][k] = {"n": n, "fixed": {a: stats_summary(list(v.values())) for a, v in delays.items()},
                           "clair": {a: stats_summary(list(v.values())) for a, v in cl.items()},
                           "hit": hit, "unwon": unwon,
                           "base_mean_k": summary_baseline(t)["mean_k"]}
        rep["interactions"][k] = {lab: {"mean": c[0], "se": c[1], "n": c[2]}
                                  for lab, c in interaction_rows(delays).items()}
        rep["skip"][k] = {}
        for arm in arms:
            if arm["kind"] == "skip":
                ok, tot, bad = skip_check(t, arm["t"], wins.get("%s__%s" % (k, arm["arm"]), {}))
                rep["skip"][k][arm["arm"]] = {"exact": ok, "total": tot, "exceptions": bad[:10]}
        cls, share = deck_class({"deck": t["deck_file"]}, t)
        rep["life"][k] = {"class": cls, "creature_share": share, "flip": {}}
        for a_ in delays:
            if a_.startswith("life"):
                rep["life"][k]["flip"][a_] = {w: race_flip(t, delays[a_], w) for w in (4, 5, 6, 7, 8)}
        rep["decks"][k]["_delays"] = delays
        rep["decks"][k]["_clair"] = cl
    return rep, rows


def format_report(rep, meta):
    L = []
    decks = sorted(rep["decks"])
    arms = []
    for k in decks:
        for a in rep["decks"][k]["fixed"]:
            if a not in arms:
                arms.append(a)
    L.append("S[e,t,g]: delay in rounds, paired per game against the stored baseline (unwon = %d); "
             "fixed policy, then clairvoyant (max over every candidate), mean (se)" % UNWON)
    L.append("decks: %s; base mean kill: %s" % (", ".join(decks), ", ".join(
        "%s %.2f" % (k, rep["decks"][k]["base_mean_k"]) for k in decks)))
    hdr = "%-14s" % "arm" + "".join(("%s" % k[:13]).rjust(14) for k in decks) + "  mean of decks   clair-mean  probe5"
    L.append(hdr)
    for a in arms:
        cells = [rep["decks"][k]["fixed"].get(a) for k in decks]
        cc = [rep["decks"][k]["clair"].get(a) for k in decks]
        m = mean_over_decks([(c["mean"], c["se"]) if c else None for c in cells])
        mc = mean_over_decks([(c["mean"], c["se"]) if c else None for c in cc])
        rep["mean"][a] = {"fixed": m, "clair": mc}
        line = "%-14s" % a + "".join(_fmt((c["mean"], c["se"])) if c else "-".rjust(14) for c in cells)
        line += "  " + _fmt(m) + "  " + (_fmt(mc) if mc[0] is not None else "      -     ")
        line += ("  %+.2f" % PROBE_MEAN5[a]) if a in PROBE_MEAN5 else ""
        L.append(line)
    L.append("")
    L.append("clairvoyant arms per deck (Q3/Q8: try every candidate, keep the largest delay), mean (se):")
    for a in arms:
        if not any(a in rep["decks"][k]["clair"] for k in decks):
            continue
        L.append("%-14s" % a + "".join(_fmt((rep["decks"][k]["clair"][a]["mean"], rep["decks"][k]["clair"][a]["se"]))
                                     if a in rep["decks"][k]["clair"] else "-".rjust(14) for k in decks))
    L.append("")
    L.append("hit rate (fixed policy: applied / games reaching the round) and unwon-at-15 games:")
    for a in arms:
        L.append("%-14s" % a + "".join(
            ("%d/%d u%d" % (rep["decks"][k]["hit"][a][0], rep["decks"][k]["hit"][a][1], rep["decks"][k]["unwon"][a])
             if a in rep["decks"][k]["hit"] else "-").rjust(14) for k in decks))
    L.append("")
    L.append("interaction = joint delay minus the sum of its parts, paired per game (section 8.2):")
    labs = [lab for lab, _, _ in INTERACTIONS] + [lab for lab, _ in INCREMENTS]
    for lab in labs:
        cells = [rep["interactions"][k].get(lab) for k in decks]
        if not any(cells):
            continue
        m = mean_over_decks([(c["mean"], c["se"]) if c else None for c in cells])
        L.append("%-30s" % lab + "".join(_fmt((c["mean"], c["se"])) if c else "-".rjust(14) for c in cells)
                 + "  " + _fmt(m) + (("  probe %+.2f" % PROBE_INTER5[lab]) if lab in PROBE_INTER5 else ""))
    L.append("")
    L.append("analytic skip check (+1 for every game not won before the round; unwon stays unwon):")
    for k in decks:
        L.append("  %-14s " % k + "  ".join("%s %d/%d%s" % (a, v["exact"], v["total"],
                                                            (" exc " + str(v["exceptions"][:3])) if v["exceptions"] else "")
                                           for a, v in sorted(rep["skip"][k].items())))
    L.append("")
    L.append("life gain by deck class (USER 5.6): mean delay of life3 / life5 per round, then race-flip share "
             "P(K < w <= K + D) at w = 4..8")
    byc = defaultdict(list)
    for k in decks:
        byc[rep["life"][k]["class"]].append(k)
    for c, ks in sorted(byc.items()):
        L.append("  class %s: %s" % (c, ", ".join("%s(%.2f)" % (k, rep["life"][k]["creature_share"] or 0) for k in ks)))
        for a in arms:
            if not a.startswith("life"):
                continue
            m = mean_over_decks([(rep["decks"][k]["fixed"][a]["mean"], rep["decks"][k]["fixed"][a]["se"])
                                 for k in ks if a in rep["decks"][k]["fixed"]])
            fl = {w: [rep["life"][k]["flip"].get(a, {}).get(w) for k in ks] for w in (4, 5, 6, 7, 8)}
            L.append("    %-8s %s   flip %s" % (a, _fmt(m), " ".join(
                "w%d %.2f" % (w, sum(x for x in v if x is not None) / max(1, len([x for x in v if x is not None])))
                for w, v in fl.items())))
    return "\n".join(L) + "\n"


def life_screen_check(meta, tables, rep):
    """For an unscreened run: of the games the section 5.6 screen would have zeroed, how many measured 0."""
    out = {}
    for k, arms in meta["arms"].items():
        t = tables[k]
        z = nz = 0
        bad = []
        for arm in arms:
            if arm["policy"] != "fixed" or "life" not in KINDS.get(arm["kind"], {}):
                continue
            d = rep["decks"][k]["_delays"][arm["arm"]]
            for g in t["games"]:
                if reached(g, arm["t"]) and life_zero(g, arm["t"], KINDS[arm["kind"]]["life"]) and g["gi"] in d:
                    if d[g["gi"]] == 0:
                        z += 1
                    else:
                        nz += 1
                        bad.append((arm["arm"], g["gi"], d[g["gi"]]))
        out[k] = (z, nz, bad[:5])
    return out


def cmd_report(a):
    with open(os.path.join(a.dir, "meta.json")) as f:
        meta = json.load(f)
    tables = load_tables(a.baseline_dir or meta["baseline_dir"], list(meta["arms"]))
    wins = {}
    for fn in os.listdir(a.dir):
        if fn.endswith(".wins"):
            wins[fn[:-5]] = read_wins(os.path.join(a.dir, fn))
    injects = defaultdict(list)
    with open(os.path.join(a.dir, "batch.err"), errors="replace") as f:
        for line in f:
            if line.startswith("[ctrl-inject] "):
                r = json.loads(line[14:])
                injects[r["job"].split("__")[0]].append(r)
    rep, rows = build_report(meta, tables, wins, injects)
    txt = format_report(rep, meta)
    if not meta.get("life_screen"):
        chk = life_screen_check(meta, tables, rep)
        txt += "life screen check (games the 5.6 screen would zero: measured 0 / nonzero):\n"
        for k, (z, nz, bad) in sorted(chk.items()):
            txt += "  %-14s %d / %d%s\n" % (k, z, nz, ("  e.g. " + str(bad)) if bad else "")
    print(txt, end="")
    if a.out:
        with open(a.out, "w") as f:
            f.write(txt)
    if a.json:
        with open(a.json, "w") as f:
            json.dump({k: v for k, v in rep.items()}, f, default=lambda o: None)
    if a.rows:
        with open(a.rows, "w") as f:
            for r in rows:
                f.write(json.dumps(r) + "\n")


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = p.add_subparsers(dest="cmd", required=True)
    b = sp.add_parser("baseline")
    bs = b.add_subparsers(dest="sub", required=True)
    bm = bs.add_parser("make")
    bm.add_argument("dir")
    bm.add_argument("--games", type=int, default=20)
    bm.add_argument("--games-for", default="")
    bm.add_argument("--decks", default="")
    bm.add_argument("--exclude", default="")
    bm.add_argument("--identity", default=",".join(PILOT_DECKS))
    bm.add_argument("--max-wall-hours", type=float, default=0.0)
    bm.add_argument("--threads", type=int, default=2)
    bm.add_argument("--horizon-factor", type=float, default=1.2)
    bi = bs.add_parser("ingest")
    bi.add_argument("dir")
    bi.add_argument("--out", default=os.path.join(ROOT, "logs/control-mode/baseline"))
    bc = bs.add_parser("check")
    bc.add_argument("--out", default=os.path.join(ROOT, "logs/control-mode/baseline"))
    pl = sp.add_parser("plan")
    pl.add_argument("--what", choices=("baseline", "table"), default="table")
    pl.add_argument("--games", type=int, default=20)
    pl.add_argument("--threads", type=int, default=2)
    pl.add_argument("--decks", default="")
    pl.add_argument("--baseline-dir", default="")
    pl.add_argument("--rounds", default="2-5")
    pl.add_argument("--no-clairvoyant", action="store_true")
    pl.add_argument("--no-joint", action="store_true")
    pl.add_argument("--life-screen", action="store_true")
    pl.add_argument("--estimate", action="store_true", help="units-scaled estimate instead of the suite max")
    pl.add_argument("--arms-per-game", type=int, default=120, help="games per base game without a baseline")
    pl.add_argument("--arm-factor", type=float, default=2.5,
                    help="cost of a disrupted game over a baseline game (pilot 2026-11-27: 2.5 cost-weighted; "
                         "Stompy 2.9, Burn 1.9, Dragons 1.6, White Knights 1.2, Breaching 1.0)")
    pl.add_argument("--horizon-factor", type=float, default=1.2)
    t = sp.add_parser("table")
    ts = t.add_subparsers(dest="sub", required=True)
    tm = ts.add_parser("make")
    tm.add_argument("dir")
    tm.add_argument("--baseline-dir", required=True)
    tm.add_argument("--decks", default="")
    tm.add_argument("--games", type=int, default=None)
    tm.add_argument("--rounds", default="2-5")
    tm.add_argument("--kinds", default="")
    tm.add_argument("--no-clairvoyant", action="store_true")
    tm.add_argument("--no-joint", action="store_true")
    tm.add_argument("--life-screen", action="store_true")
    tm.add_argument("--allow-stale", action="store_true")
    r = sp.add_parser("report")
    r.add_argument("dir")
    r.add_argument("--baseline-dir", default="")
    r.add_argument("--out", default="")
    r.add_argument("--json", default="")
    r.add_argument("--rows", default="")
    a = p.parse_args(argv)
    if a.cmd == "plan":
        return cmd_plan(a)
    if a.cmd == "baseline":
        return {"make": cmd_baseline_make, "ingest": cmd_baseline_ingest, "check": cmd_baseline_check}[a.sub](a)
    if a.cmd == "table":
        return cmd_table_make(a)
    if a.cmd == "report":
        return cmd_report(a)


if __name__ == "__main__":
    main()

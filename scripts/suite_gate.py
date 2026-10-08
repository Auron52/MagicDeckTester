#!/usr/bin/env python3
"""REGRESSION-SUITE MEMBERSHIP GATE -- a required step of deck analysis, and a HARD BLOCK on the
value-leaf and mulligan-profile generators.

WHY THIS EXISTS (user directive, 2026-09-26)
--------------------------------------------
Adding a deck to `test/regression_cases.sh` was documented as a step of `analyze-deck` but nothing
enforced it, so it was simply skipped. The failure is quiet and compounding:

  * **Nothing watches the deck's play.** `verify_deck.py` already says so, in a DISCLOSURE nobody has
    to act on: *"<deck> is NOT a regression case, so NO digest tracks its play -- nothing will tell you
    when this record goes stale."* A deck outside the suite has no ground truth, so no engine change
    can ever be shown to have broken it.
  * **The expensive stages get spent on unwatched play.** A value leaf and a mulligan table are fitted
    TO the deck's play and are invalidated by play-logic changes. Generating them for a deck with no GT
    is spending hours on an artifact whose foundation nothing is checking.
  * **The generators' own validation silently degrades.** `mullgen.sh`'s regression step just logs
    `not in test/regression_cases.sh -- skipping (nothing to move)` and returns 0, so the adoption
    reports PASSED having run one fewer check than it does for every other deck. That happened on
    WhiteKnights (2026-09-26), which is what prompted this gate.

So suite membership is now a PRECONDITION, enforced early and loudly, not a to-do item.

THE COST RULE (user, 2026-09-26)
--------------------------------
A deck is only added if its tested cost is **no more than 3x the most expensive deck that already has
BOTH a value leaf and a mulligan profile** (in practice that reference deck is often Hinata). Those
decks are the right yardstick because they are the ones that have been through this whole pipeline --
they define what the suite has already been shown to absorb.

If the candidate is over 3x, the answer is NOT to add it anyway and NOT to skip the suite: it is
reported to the user, and **making the deck fit becomes the first goal** -- i.e. optimisation work
comes before the value leaf and the mulligan profile.

WHAT IS MEASURED, and why it is this and not total tier time
------------------------------------------------------------
`cost_per_game_ms` = the **maximum per-game core-ms over the deck's SEARCHED (depth>0) cases** in the
tier. Reasons:

  * It cannot be gamed by sizing. Total tier core-ms is the thing the budget actually cares about, but
    it is the product of intrinsic cost AND the game counts we chose -- so a pathologically slow deck
    given 5 games per case would "pass" a total-cost gate while still being the slowest deck in the
    repo. Per-game cost is the deck's own expense and is invariant to that choice.
  * It is comparable across decks that ship different budgets, because the budget a deck ships IS part
    of what it costs to test.
  * d0 cases are excluded: they are ~0 ms everywhere and would wash out the comparison.

Total tier core-ms is measured and reported alongside, because it is what consumes the budget -- it is
just not the gated number.

Measurements are cached in `test/suite_cost.json` so this is cheap to re-run. Each deck is measured by
a tier run FILTERED to that deck (`regression.sh --deck=<key>`), which is seconds-to-minutes, not the
45-minute full tier.

USAGE
-----
    python3 scripts/suite_gate.py --require decks/<Deck>     # exit 3 if not in the suite (the BLOCK)
    python3 scripts/suite_gate.py --cost    decks/<Deck>     # measure + apply the 3x rule
    python3 scripts/suite_gate.py --measure decks/<Deck>     # (re)measure one deck into the cache
    python3 scripts/suite_gate.py --report                   # the whole cost table

Exit codes: 0 ok · 2 usage/internal · 3 NOT IN SUITE (blocking) · 4 over the cost multiple.
"""
import argparse
import json
import os
import re
import shlex
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CASES = os.path.join(ROOT, "test/regression_cases.sh")
COST_CACHE = os.path.join(ROOT, "test/suite_cost.json")
MULTIPLE = 3.0
TIER = "regression"


# --------------------------------------------------------------------------- cases file
def _read_map(name):
    """-> {key: path} for a `declare -A <name>=( [k]=v ... )` block in regression_cases.sh.

    Parsed rather than sourced so this tool works without bash, and QUOTE-STRIPPED because the map
    value is quoted for every deck whose path contains a space ("Mirrorwing Dragon", "Melira Pod").
    mullgen.sh once reported "not in the suite" for decks that were in all three tiers by missing
    exactly that -- so the same trap is called out here.
    """
    txt = open(CASES).read()
    m = re.search(rf"declare -A {name}=\((.*?)\n\)", txt, re.S)
    if not m:
        raise SystemExit(f"{CASES}: could not find `declare -A {name}=(`")
    out = {}
    for k, v in re.findall(r"\[([A-Za-z0-9_]+)\]=(\"[^\"]*\"|\S+)", m.group(1)):
        out[k] = v.strip('"')
    return out


def _cases(tier):
    """-> [(deckkey, depth, seed, games, budget)] for SMOKE_CASES / REGRESSION_CASES / OVERNIGHT_CASES."""
    var = {"smoke": "SMOKE_CASES", "regression": "REGRESSION_CASES", "overnight": "OVERNIGHT_CASES"}[tier]
    txt = open(CASES).read()
    m = re.search(rf"^{var}=\((.*?)\n\)", txt, re.S | re.M)
    if not m:
        raise SystemExit(f"{CASES}: could not find {var}")
    out = []
    for line in m.group(1).splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        mm = re.match(r'"\s*([A-Za-z0-9_]+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s*"', line)
        if mm:
            out.append((mm.group(1), int(mm.group(2)), int(mm.group(3)),
                        int(mm.group(4)), int(mm.group(5))))
    return out


def deck_key(deck_path):
    """-> the suite key for a decklist path, or None. Matches DECK_FILE values exactly."""
    rel = os.path.relpath(os.path.abspath(deck_path), ROOT)
    for k, v in _read_map("DECK_FILE").items():
        if v == rel:
            return k
    return None


def _on_disk(d, name):
    """-> `name` as it is spelled in directory `d` (exact first, else the unique case-insensitive
    match), or None. NOT os.path.exists: on a case-insensitive filesystem (this container's
    /workspaces) exists("decks/Soldiers/Soldiers.cod") is True for soldiers.cod, and the
    wrong-case path then misses DECK_FILE's exact value -- the gate refused Soldiers, a deck in all
    three tiers, as "NOT in the regression suite" (2026-10-08). Same rule as deck_registry.ci_entry."""
    try:
        entries = os.listdir(d)
    except OSError:
        return None
    if name in entries:
        return name
    hits = [e for e in entries if e.lower() == name.lower()]
    return hits[0] if len(hits) == 1 else None


def resolve_decklist(arg):
    """Accept a deck DIRECTORY or a decklist path; -> the decklist path, spelled as on disk.
    Mirrors mullgen.sh."""
    p = os.path.abspath(arg)
    if os.path.isfile(p):
        d, f = os.path.split(p)
        return os.path.join(d, _on_disk(d, f) or f)
    if os.path.isdir(p):
        stem = os.path.basename(p.rstrip("/"))
        for ext in (".cod", ".txt"):
            hit = _on_disk(p, stem + ext)
            if hit:
                return os.path.join(p, hit)
        for f in sorted(os.listdir(p)):
            if f.endswith((".cod", ".txt")):
                return os.path.join(p, f)
    raise SystemExit(f"no decklist found at {arg}")


# --------------------------------------------------------------------------- artifacts
def has_both_artifacts(deck_path):
    """Does this deck ship BOTH a value leaf and an exhaustive mulligan profile?

    Presence is the test, because presence IS adoption for both artifacts (the engine is
    presence-gated on each). `.gz` counts: it is the committed form.
    """
    d = os.path.dirname(os.path.abspath(deck_path))
    stem = os.path.splitext(os.path.basename(deck_path))[0]
    leaf = os.path.join(d, f"{stem}.value.json")
    prof = os.path.join(d, f"{stem}.keepmodel.exhaustive.profile.json")
    has_leaf = os.path.exists(leaf)
    has_prof = os.path.exists(prof) or os.path.exists(prof + ".gz")
    return has_leaf, has_prof


# --------------------------------------------------------------------------- cost
def load_cache():
    if os.path.exists(COST_CACHE):
        try:
            return json.load(open(COST_CACHE))
        except (ValueError, OSError):
            pass
    return {"_note": "Per-deck suite cost, written by scripts/suite_gate.py. "
                     "cost_per_game_ms = max per-game core-ms over the deck's searched (depth>0) "
                     f"{TIER} cases; total_core_ms = the whole tier for that deck.",
            "_multiple": MULTIPLE, "decks": {}}


def save_cache(c):
    json.dump(c, open(COST_CACHE, "w"), indent=1, sort_keys=True)
    with open(COST_CACHE, "a") as f:
        f.write("\n")


def parse_batch_log(path, key, tier):
    """-> {jobname: (games, ms)} for this deck's jobs in a batch log."""
    out = {}
    if not os.path.exists(path):
        return out
    pat = re.compile(r"^(\S+): played=(\d+) avg=[\d.]+ digest=\S+ ms=(\d+)")
    for line in open(path, errors="replace"):
        m = pat.match(line.strip())
        if not m:
            continue
        name = m.group(1)
        if name.startswith(f"{key}_{tier}_") or name.startswith(f"{key}2hg_{tier}_"):
            out[name] = (int(m.group(2)), int(m.group(3)))
    return out


def measure(deck_path, key, tier=TIER, run=True):
    """Run the tier FILTERED to this deck and record its cost. -> the cache entry."""
    keys = [key]
    if any(c[0] == f"{key}2hg" for c in _cases(tier)):
        keys.append(f"{key}2hg")
    log = os.path.join(ROOT, f"test/logs/{tier}/batch.log")
    if run:
        cmd = ["bash", "test/regression.sh"]
        if tier != "regression":
            cmd.append(f"--{tier}")
        cmd.append("--deck=" + ",".join(keys))
        print(f"  measuring: {' '.join(shlex.quote(c) for c in cmd)}", flush=True)
        p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        if p.returncode not in (0, 1):   # 1 = a tier FAIL, which still produced timings
            sys.stderr.write(p.stdout[-3000:] + p.stderr[-2000:])
            raise SystemExit(f"regression.sh failed rc={p.returncode} while measuring {key}")
    jobs = parse_batch_log(log, key, tier)
    if not jobs:
        raise SystemExit(f"no {tier} timings for {key} in {log} -- run "
                         f"`bash test/regression.sh --deck={key}` first")
    searched = {}
    for name, (games, ms) in jobs.items():
        m = re.search(r"_d(\d+)_s\d+$", name)
        if m and int(m.group(1)) > 0 and games:
            searched[name] = ms / games
    return {"key": key, "tier": tier,
            "cost_per_game_ms": round(max(searched.values()), 4) if searched else 0.0,
            "worst_case": max(searched, key=searched.get) if searched else None,
            "total_core_ms": sum(ms for _g, ms in jobs.values()),
            "jobs": len(jobs)}


def qualifying_costs(cache, exclude=None):
    """-> {key: entry} for suite decks that have BOTH artifacts (the yardstick set)."""
    files = _read_map("DECK_FILE")
    out = {}
    for k, rel in files.items():
        if k == exclude:
            continue
        p = os.path.join(ROOT, rel)
        if not os.path.exists(p):
            continue
        leaf, prof = has_both_artifacts(p)
        if leaf and prof and k in cache["decks"]:
            out[k] = cache["decks"][k]
    return out


# --------------------------------------------------------------------------- modes
BLOCK_MSG = """\
================================================================================
BLOCKED: {stem} is NOT in the regression suite.
================================================================================
Suite membership is a REQUIRED step of deck analysis and a precondition for BOTH
expensive stages (user directive, 2026-09-26). It is not a to-do item to carry.

WHY this blocks rather than warns: a deck outside the suite has NO ground truth,
so nothing detects when an engine change breaks its play -- and a value leaf and a
mulligan table are fitted TO that play. Generating them first spends hours on an
artifact whose foundation nothing is checking. `mullgen.sh` also silently drops its
own regression step for such a deck and still reports PASSED.

TO UNBLOCK:
  1. Measure the deck's cost and apply the 3x rule:
         python3 scripts/suite_gate.py --cost {deck}
     A deck may be added only if its cost is <= {mult:g}x the most expensive deck that
     already has both a value leaf and a mulligan profile.
  2. If it PASSES: add [{guess}] to DECK_FILE and DECK_PROF in
     test/regression_cases.sh, add cases to SMOKE_CASES / REGRESSION_CASES /
     OVERNIGHT_CASES (size from a timing probe; mirror a deck of similar speed),
     then run each tier and `--accept` to create its ground truth.
     See .claude/skills/regression-testing.md "Adding a deck to the suite".
  3. If it FAILS the 3x rule: do NOT add it and do NOT skip the suite. REPORT IT TO
     THE USER -- getting the deck into that range becomes the FIRST GOAL, ahead of
     the value leaf and the mulligan profile.
================================================================================"""


def mode_require(deck_path):
    key = deck_key(deck_path)
    stem = os.path.splitext(os.path.basename(deck_path))[0]
    if key:
        print(f"suite gate: {stem} is regression key `{key}` -- ok")
        return 0
    guess = re.sub(r"[^a-z0-9_]", "", stem.lower()) or "deckkey"
    sys.stderr.write(BLOCK_MSG.format(stem=stem, deck=os.path.relpath(os.path.dirname(
        os.path.abspath(deck_path)), ROOT), guess=guess, mult=MULTIPLE) + "\n")
    return 3


def mode_cost(deck_path, remeasure=True):
    key = deck_key(deck_path)
    stem = os.path.splitext(os.path.basename(deck_path))[0]
    cache = load_cache()
    print(f"== suite cost gate: {stem} (rule: <= {MULTIPLE:g}x the most expensive deck with BOTH "
          f"a value leaf and a mulligan profile)\n")

    if key is None:
        # A deck not yet in the suite has no cases to time, which is the chicken-and-egg this rule
        # has to survive: the cost must be knowable BEFORE the deck is added. Say so plainly and
        # point at the probe route rather than pretending to measure.
        print(f"   {stem} has no suite cases yet, so its cost cannot be read from a tier run.")
        print("   Add provisional cases first (they can be tuned after), or time the deck directly")
        print("   with a pooled batch at the configurations you intend to ship, then re-run --cost.")
    else:
        ent = measure(deck_path, key, run=remeasure)
        cache["decks"][key] = ent
        save_cache(cache)
        print(f"   candidate {key}: {ent['cost_per_game_ms']:.3f} ms/game "
              f"(worst searched case {ent['worst_case']}), total {ent['total_core_ms']/1000:.1f} core-s "
              f"over {ent['jobs']} jobs\n")

    q = qualifying_costs(cache, exclude=key)
    if not q:
        print("   NO reference deck has both artifacts AND a recorded cost yet.")
        print("   Record some with:  python3 scripts/suite_gate.py --measure decks/<Deck>")
        print("   VERDICT: UNGATED -- the rule cannot be applied, so it is not claiming a pass.")
        return 0

    ref = max(q, key=lambda k: q[k]["cost_per_game_ms"])
    refc = q[ref]["cost_per_game_ms"]
    print(f"   {'deck':<16} {'ms/game':>10}   (decks with BOTH a value leaf and a mulligan profile)")
    for k in sorted(q, key=lambda k: -q[k]["cost_per_game_ms"]):
        print(f"   {k:<16} {q[k]['cost_per_game_ms']:>10.3f}" + ("   <- reference" if k == ref else ""))
    print()
    if key is None:
        print(f"   reference: {ref} at {refc:.3f} ms/game -> budget for a new deck is "
              f"{MULTIPLE * refc:.3f} ms/game")
        return 0
    cand = cache["decks"][key]["cost_per_game_ms"]
    ratio = (cand / refc) if refc else float("inf")
    print(f"   {key} / {ref} = {cand:.3f} / {refc:.3f} = {ratio:.2f}x   (limit {MULTIPLE:g}x)")
    if ratio <= MULTIPLE:
        print(f"\n   VERDICT: PASS -- {key} may be added to the suite.")
        return 0
    print(f"\n   VERDICT: FAIL -- {key} is {ratio:.2f}x the reference, over the {MULTIPLE:g}x limit.")
    print("   Do NOT add it and do NOT skip the suite. REPORT THIS TO THE USER: getting this deck")
    print("   into range is the FIRST GOAL, ahead of the value leaf and the mulligan profile.")
    return 4


def mode_measure_all(tier=TIER, run=True):
    """Record EVERY suite deck's cost from ONE pooled full-tier run.

    Deliberately one run, not a loop of per-deck `--deck=` runs. 23 filtered runs would pay 23
    load-imbalance tails and idle the box between them -- precisely the "WAVES ARE A LOOP" defect
    CLAUDE.md forbids, and for no benefit: the full tier already pools every deck's cases into one
    queue, so its single batch.log carries all of their timings at once. It also costs less wall-clock
    than the loop it replaces.

    Never accepts: this only reads timings, so ground truth is untouched even if the tier reports drift.
    """
    log = os.path.join(ROOT, f"test/logs/{tier}/batch.log")
    if run:
        cmd = ["bash", "test/regression.sh"] + ([] if tier == "regression" else [f"--{tier}"])
        print(f"  ONE pooled tier run (no --accept, so GT is untouched): {' '.join(cmd)}", flush=True)
        p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        if p.returncode not in (0, 1):
            sys.stderr.write(p.stdout[-3000:] + p.stderr[-2000:])
            raise SystemExit(f"regression.sh failed rc={p.returncode}")
        tail = [l for l in p.stdout.splitlines() if l.startswith(("Result:", "ALL PASS", "REGRESSION"))]
        for l in tail:
            print("  " + l)
    cache = load_cache()
    n = 0
    for key in sorted(_read_map("DECK_FILE")):
        rel = _read_map("DECK_FILE")[key]
        p = os.path.join(ROOT, rel)
        if not os.path.exists(p):
            continue
        try:
            cache["decks"][key] = measure(p, key, tier=tier, run=False)
            n += 1
        except SystemExit:
            continue                      # no cases for this deck in this tier
    save_cache(cache)
    print(f"\nrecorded {n} deck(s) -> {os.path.relpath(COST_CACHE, ROOT)}")
    return mode_report()


def mode_measure(deck_path):
    key = deck_key(deck_path)
    if key is None:
        sys.stderr.write(f"{deck_path} is not in the suite -- nothing to measure\n")
        return 3
    cache = load_cache()
    cache["decks"][key] = measure(deck_path, key)
    save_cache(cache)
    e = cache["decks"][key]
    print(f"{key}: {e['cost_per_game_ms']:.3f} ms/game (worst {e['worst_case']}), "
          f"{e['total_core_ms']/1000:.1f} core-s -> {os.path.relpath(COST_CACHE, ROOT)}")
    return 0


def mode_report():
    cache = load_cache()
    files = _read_map("DECK_FILE")
    print(f"{'deck':<16} {'ms/game':>10} {'core-s':>9}  leaf  mull   worst case")
    rows = []
    for k, rel in sorted(files.items()):
        p = os.path.join(ROOT, rel)
        leaf, prof = has_both_artifacts(p) if os.path.exists(p) else (False, False)
        e = cache["decks"].get(k)
        rows.append((k, e, leaf, prof))
    for k, e, leaf, prof in sorted(rows, key=lambda r: -(r[1]["cost_per_game_ms"] if r[1] else -1)):
        if e:
            print(f"{k:<16} {e['cost_per_game_ms']:>10.3f} {e['total_core_ms']/1000:>9.1f}"
                  f"  {'Y' if leaf else '-':^4}  {'Y' if prof else '-':^4}   {e['worst_case'] or ''}")
        else:
            print(f"{k:<16} {'-':>10} {'-':>9}  {'Y' if leaf else '-':^4}  {'Y' if prof else '-':^4}"
                  f"   (not measured)")
    q = qualifying_costs(cache)
    if q:
        ref = max(q, key=lambda k: q[k]["cost_per_game_ms"])
        print(f"\nreference (most expensive with BOTH artifacts): {ref} at "
              f"{q[ref]['cost_per_game_ms']:.3f} ms/game -> new-deck budget "
              f"{MULTIPLE * q[ref]['cost_per_game_ms']:.3f} ms/game")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--require", metavar="DECK", help="exit 3 if the deck is not a regression case")
    g.add_argument("--cost", metavar="DECK", help="measure and apply the 3x rule")
    g.add_argument("--measure", metavar="DECK", help="(re)measure one deck into the cost cache")
    g.add_argument("--measure-all", action="store_true",
                   help="record every suite deck from ONE pooled full-tier run (never accepts)")
    g.add_argument("--report", action="store_true", help="print the whole cost table")
    ap.add_argument("--no-run", action="store_true",
                    help="with --cost: reuse the last tier log instead of re-running")
    a = ap.parse_args()
    if a.report:
        return mode_report()
    if a.measure_all:
        return mode_measure_all(run=not a.no_run)
    if a.require:
        return mode_require(resolve_decklist(a.require))
    if a.cost:
        return mode_cost(resolve_decklist(a.cost), remeasure=not a.no_run)
    return mode_measure(resolve_decklist(a.measure))


if __name__ == "__main__":
    sys.exit(main())

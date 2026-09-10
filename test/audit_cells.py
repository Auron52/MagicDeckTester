#!/usr/bin/env python3
"""Per-CELL decomposition of a run vs ground truth — the analysis an `--accept` actually needs.

WHY THIS EXISTS, and it is a USER RULING, not a preference: *"We can rebaseline changes that aren't
yours, but we need to analyze the differences to ensure they are improvements."* The gate on promoting
ground truth is EVIDENCE, and **the tier aggregate cannot supply it**. Worked case, 2026-09-10
overnight: 102 better games vs 28 worse, net -82 turns -- a clean-looking win that CONCEALED the only
two cells that were net worse (mirrorwing +4, fluctuator +2). Nothing in the tier total surfaces
those; you have to group.

`audit_changed_games.py` answers "how many games moved, and which slower ones do I read?" -- split
searched vs d0, which is the right split for choosing what to inspect. This answers the other
question: **"is any DECK x DEPTH cell net worse, and by how much?"** -- which is where a regression
that the tier total drowns will show up, because a deck's games all share one apparatus and one
policy, so a real defect concentrates in a cell rather than smearing across the tier.

Reports every changed cell sorted WORST FIRST, with its per-game breakdown, so the net-worse cells are
the first thing on screen. Report-only, always exits 0: the accept decision is a judgement about
whether each difference is an improvement, and this lays out the differences to judge.

Usage:
    python3 test/audit_cells.py <smoke|regression|overnight>
    python3 test/audit_cells.py <mode> --old-git    # baseline = HEAD~1 gt_logs (re-check an accept)
    python3 test/audit_cells.py <mode> --old-rev=<rev>   # baseline = that revision's gt_logs
    python3 test/audit_cells.py <mode> --games      # also list every changed game per cell
"""
import sys, os, glob, subprocess
from collections import defaultdict

MODE = next((a for a in sys.argv[1:] if not a.startswith("-")), None)
if MODE not in ("smoke", "regression", "overnight"):
    print("usage: audit_cells.py <smoke|regression|overnight> [--old-git] [--games]"); sys.exit(2)
OLD_GIT = "--old-git" in sys.argv or any(a.startswith("--old-rev=") for a in sys.argv)
# Which revision's gt_logs are the baseline. `--old-git` means HEAD~1 (re-check the accept you just
# made); `--old-rev=<rev>` names one explicitly, which is what you want when the accept is several
# commits back or you are attributing across someone else's work.
OLD_REV = next((a.split("=", 1)[1] for a in sys.argv if a.startswith("--old-rev=")), "HEAD~1")
SHOW_GAMES = "--games" in sys.argv

# Loss-penalized ORDER key, identical to audit_changed_games.py: a won game scores its win turn, an
# unwon game ranks worse than any win. Kept horizon-agnostic -- only the ordering is used.
LOSS = 10_000


def read_wins(text):
    out = {}
    for l in text.splitlines():
        p = l.split()
        if p:
            out[int(p[0])] = (int(p[1]), p[2] if len(p) > 2 else None)
    return out


def score(w):
    return w if w > 0 else LOSS


def cell_of(key):
    """<deck>_<mode>_d<depth>_s<seed> -> (deck, depth). Seeds are POOLED into the cell on purpose:
    the same deck at the same depth under two seeds is one policy measured twice, so splitting them
    just halves the sample that a verdict rests on."""
    stem = key.rsplit("_s", 1)[0]
    deck, _, dpart = stem.rpartition(f"_{MODE}_")
    return (deck or stem, dpart or "d?")


def old_side(key):
    if OLD_GIT:
        r = subprocess.run(["git", "show", f"{OLD_REV}:test/gt_logs/{key}.wins"],
                           capture_output=True, text=True)
        return read_wins(r.stdout) if r.returncode == 0 else {}
    p = f"test/gt_logs/{key}.wins"
    return read_wins(open(p).read()) if os.path.exists(p) else {}


def new_side(key):
    p = f"test/gt_logs/{key}.wins" if OLD_GIT else f"test/logs/{MODE}/wins/{key}.wins"
    return read_wins(open(p).read()) if os.path.exists(p) else {}


keys = sorted(os.path.basename(f)[:-5] for f in glob.glob(f"test/gt_logs/*_{MODE}_*.wins"))
if not keys:
    print(f"no committed gt_logs for mode '{MODE}'"); sys.exit(2)

cells = defaultdict(lambda: dict(better=0, worse=0, played=0, net=0, games=[], keys=set()))
missing = 0
for key in keys:
    old, new = old_side(key), new_side(key)
    if not new:
        missing += 1
        continue
    c = cells[cell_of(key)]
    c["keys"].add(key)
    for gi, (n, nd) in new.items():
        ov = old.get(gi)
        if ov is None:
            continue
        o, od = ov
        so, sn = score(o), score(n)
        if sn == so:
            if od is not None and nd is not None and od != nd:
                c["played"] += 1
            continue
        # Net is in TURNS, so an unwon game contributes its loss score -- the same
        # loss-penalized metric the suite reports, not a win-only average.
        c["net"] += sn - so
        if sn > so:
            c["worse"] += 1
        else:
            c["better"] += 1
        c["games"].append((key, gi, o, n))

changed = {k: v for k, v in cells.items() if v["better"] or v["worse"] or v["played"]}
base = f"{OLD_REV} gt_logs" if OLD_GIT else "committed gt_logs"
print(f"CELLS {MODE}   (old = {base}   new = "
      f"{'current gt_logs' if OLD_GIT else f'test/logs/{MODE}/wins'})")
print(f"  cells: {len(cells)}   changed: {len(changed)}   no-run-dir keys: {missing}\n")
if not changed:
    print("  no cell changed."); sys.exit(0)

# WORST FIRST -- a net-worse cell is the thing this report exists to surface, so it must not be
# somewhere in the middle of an alphabetical list.
print(f"  {'deck':<20}{'depth':>6}{'better':>8}{'worse':>7}{'played':>8}{'net turns':>11}")
net_total = worse_cells = 0
for (deck, depth), v in sorted(changed.items(), key=lambda kv: -kv[1]["net"]):
    net_total += v["net"]
    flag = ""
    if v["net"] > 0:
        flag = "   <-- NET WORSE: needs a per-game verdict"
        worse_cells += 1
    elif v["net"] < 0:
        flag = "   better"
    print(f"  {deck:<20}{depth:>6}{v['better']:>8}{v['worse']:>7}{v['played']:>8}"
          f"{v['net']:>+11}{flag}")
    if SHOW_GAMES or v["net"] > 0:
        for key, gi, o, n in sorted(v["games"], key=lambda g: -(score(g[3]) - score(g[2]))):
            if score(n) == score(o):
                continue
            tag = "WORSE" if score(n) > score(o) else "better"
            print(f"        {tag:<7}{key} gi{gi}: {o if o>0 else 'unwon'} -> {n if n>0 else 'unwon'}")

print(f"\n  NET {net_total:+} turns over {len(changed)} changed cells; "
      f"{worse_cells} cell(s) NET WORSE.")
if worse_cells:
    print("  A net-worse cell is not automatically a regression -- classify its slower games first\n"
          "  (`bash test/classify_turn_later.sh %s`: churn recovers at 4x/16x budget), and check\n"
          "  whether the draws diverged (a physically different game is not comparable per-game).\n"
          "  But it does need a stated verdict before --accept, and the tier total will not give one."
          % MODE)

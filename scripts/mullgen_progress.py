#!/usr/bin/env python3
"""Report mulligan-generation progress in R, not in phases.

    python3 scripts/mullgen_progress.py decks/Snow [logs/Snow_mullgen/gen.log]

WHY THIS EXISTS. The monitor line reports `phase=`, `subwave=`, `roll7`/`rollsub` and `frozen`,
which between them need a mental model of the generator to read -- and misreading `phase=` once
cost two badly wrong ETAs on the Snow run (see docs/design/snow-mullgen-secondary-handoff.md
section 9.4h). The unit of work is a cell-side's rollout count R: generation is finished when
every cell-side's R is FINAL. That is phase-independent and is what this prints.

THE ONE THING TO KNOW: the schedule is ADAPTIVE. A cell stops at whatever R settles its decision,
floor 2, cap R from the recipe (fast=30, complete=40). So "% of cap" is NOT completion -- most
cells are meant to stop far below cap. The real completion number is the share of cell-sides whose
R is locked, which for the size-7 keep side is the engine's own `frozen` counter.
"""
import collections
import json
import re
import sys
from pathlib import Path


def load_journal(path):
    """-> {(H, i, p): final R}, taking max n per cell-side (one record per refine step)."""
    best = {}
    with open(path, errors="ignore") as fh:
        for line in fh:
            line = line.strip()
            if not line.startswith("{") or '"H"' not in line:
                continue
            try:
                r = json.loads(line)
            except ValueError:
                continue
            try:
                key = (r["H"], r["i"], r["p"])
            except KeyError:
                continue
            n = r.get("n", 0)
            if n > best.get(key, 0):
                best[key] = n
    return best


def last_heartbeat(log):
    """-> dict of the final monitor line's fields, or {}."""
    out = {}
    if not log or not Path(log).exists():
        return out
    pat = re.compile(
        r"monitor:\s*(?P<t>\d+)s\s+phase=(?P<phase>\w+).*?"
        r"roll7=(?P<roll7>\d+).*?rollsub=(?P<rollsub>\d+).*?"
        r"frozen=(?P<frozen>\d+)/(?P<frozen_tot>\d+)"
    )
    with open(log, errors="ignore") as fh:
        for line in fh:
            m = pat.search(line)
            if m:
                out = {k: (int(v) if k != "phase" else v) for k, v in m.groupdict().items()}
    return out


def bar(frac, width=34):
    filled = int(round(frac * width))
    return "[" + "#" * filled + "." * (width - filled) + "]"


def report(title, cells, cap, locked=None):
    if not cells:
        return 0, 0
    hist = collections.Counter(cells.values())
    tot = len(cells)
    units = sum(n * c for n, c in hist.items())
    print(f"\n=== {title}: {tot:,} cell-sides, cap R={cap}")
    cum = 0
    for n in sorted(hist):
        cum += hist[n]
        print(f"    R={n:>3}: {hist[n]:>8,}  {hist[n] / tot * 100:5.2f}%   cum {cum / tot * 100:6.2f}%")
    print(f"    mean R {units / tot:5.2f} of cap {cap}   R-units banked {units:,}")
    if locked is None:
        print(f"    R FINAL for all {tot:,} cell-sides  {bar(1.0)} 100.0%")
        locked = tot
    else:
        print(f"    R FINAL for {locked:,}/{tot:,}      {bar(locked / tot)} {locked / tot * 100:.1f}%")
    return units, locked


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    deck = Path(sys.argv[1])
    stem = deck.name
    journal = deck / f"{stem}.keepmodel.exhaustive.raw.json.journal"
    if not journal.exists():
        sys.exit(f"no journal at {journal}")
    log = sys.argv[2] if len(sys.argv) > 2 else f"logs/{stem}_mullgen/gen.log"

    hb = last_heartbeat(log)
    best = load_journal(journal)
    keep = {k: v for k, v in best.items() if k[0] == 7}
    sub = {k: v for k, v in best.items() if k[0] != 7}

    cap = 30
    print(f"deck {stem}   journal {journal.stat().st_size / 1048576:.1f} MB")
    if hb:
        print(
            f"last heartbeat: h{hb['t'] / 3600:.1f}  phase={hb['phase']}  "
            f"frozen={hb['frozen']:,}/{hb['frozen_tot']:,}"
        )

    # The size-7 keep side's R is locked exactly when the engine freezes the cell. The sub side
    # has no per-cell freeze readout; it is locked once the sub waves converge (`subwave=N conv`).
    frozen = hb.get("frozen") if hb else None
    sub_conv = bool(hb) and "conv" in open(log, errors="ignore").read()[-400000:]

    u1, l1 = report("SIZE-7 keep", keep, cap, locked=frozen)
    u2, l2 = report("SUB bottoming", sub, cap, locked=None if sub_conv else 0)

    tot_cells = len(keep) + len(sub)
    locked = l1 + l2
    print(f"\n=== WHOLE TABLE")
    print(f"    R-units banked      {u1 + u2:,}")
    print(f"    cell-sides R-FINAL  {locked:,}/{tot_cells:,}  {bar(locked / tot_cells)} "
          f"{locked / tot_cells * 100:.1f}%   <-- completion")
    if frozen is not None and len(keep) > frozen:
        print(f"    still climbing      {len(keep) - frozen:,} keep cell-sides")
    print("\n    NOTE mean R is not progress: the schedule is adaptive, so most cells stop far")
    print("    below cap by design (keep is robust at low R; bottoming needs high R).")


if __name__ == "__main__":
    main()

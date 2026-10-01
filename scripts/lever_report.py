#!/usr/bin/env python3
"""Read a `lever_sweep.sh` output tree and report WORK and QUALITY per arm.

    python3 scripts/lever_report.py logs/leversweep/bottomeval

WHY THE COLUMNS ARE THESE COLUMNS.  `units` is the budget's own unit (1 per search node), but a node
drags several mana-payment solves behind it and that ratio varies ~119x across decisions, so a lever
that frees units can read flat on `units` while real work moved -- and vice versa.  `pay_calls` is
the uncharged half, `cand_scored` is the branching the lever actually targets, and `cpu` is the only
figure that cannot be gamed by a charging convention.  `avg turns` sits beside them because a lever
that removes work and loses turns is not a win; it is reported PER CHUNK as well as pooled, because
the pooled mean hides a one-game move in a 600-game denominator.

PER-CHUNK EQUALITY IS THE QUALITY TEST, NOT THE POOLED MEAN.  The metric's quantum is 1/games, so a
pooled delta smaller than 1/(chunks*games) cannot be a real game at all (memory
`delta-at-the-metric-quantum`).  The table therefore prints, for every arm, how many chunks differ
from base -- which is a count of chunks containing at least one changed game, and the only honest
"did play move" read available without per-game logs.
"""

import os
import re
import sys
from collections import defaultdict

PATS = {
    "units":  re.compile(r"units_total=(\d+)"),
    "cands":  re.compile(r"cand_scored=(\d+)"),
    "pay":    re.compile(r"ACT_HOLD_COST\s+pay_calls=(\d+)"),
    "turns":  re.compile(r"avg \(turns\)\s*:\s*([0-9.]+)"),
    "games":  re.compile(r"Games played\s*:\s*(\d+)"),
}


def read_chunk(path):
    out = {}
    with open(path, errors="replace") as f:
        for line in f:
            for k, p in PATS.items():
                if k in out:
                    continue
                m = p.search(line)
                if m:
                    out[k] = float(m.group(1)) if k == "turns" else int(m.group(1))
    return out


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "logs/leversweep"
    arms = sorted(d for d in os.listdir(root) if os.path.isdir(os.path.join(root, d)))
    if not arms:
        sys.exit(f"no arm directories under {root}")
    data = {}
    for a in arms:
        chunks = {}
        for fn in sorted(os.listdir(os.path.join(root, a))):
            m = re.match(r"chunk_(\d+)\.log$", fn)
            if not m:
                continue
            c = read_chunk(os.path.join(root, a, fn))
            if "games" in c:
                chunks[int(m.group(1))] = c
        data[a] = chunks

    # Base is whichever arm is literally named `base`, else the first alphabetically -- named
    # rather than positional so a renamed arm cannot silently become the reference.
    base = "base" if "base" in data else arms[0]
    common = set(data[base])
    for a in arms:
        common &= set(data[a])
    common = sorted(common)
    if not common:
        sys.exit("no chunk index is present in every arm -- the arms are not comparable")
    missing = {a: sorted(set(data[a]) - set(common)) for a in arms}
    for a, ms in missing.items():
        if ms:
            print(f"# NOTE: arm {a} has extra/unmatched chunks {ms} -- excluded from every total")

    def tot(a, k):
        return sum(data[a][c].get(k, 0) for c in common)

    games = tot(base, "games")
    print(f"# {len(common)} chunks x {games // len(common)} games = {games} games per arm "
          f"(arms: {', '.join(arms)}; base = {base})")
    hdr = (f"{'arm':>10} {'units':>14} {'d%':>8} {'cand_scored':>14} {'d%':>8} "
           f"{'pay_calls':>14} {'d%':>8} {'avg turns':>10} {'d':>9} {'chunks!=base':>13}")
    print(hdr)
    print("-" * len(hdr))
    b = {k: tot(base, k) for k in ("units", "cands", "pay")}
    bturn = sum(data[base][c]["turns"] * data[base][c]["games"] for c in common) / games
    for a in arms:
        v = {k: tot(a, k) for k in ("units", "cands", "pay")}
        aturn = sum(data[a][c]["turns"] * data[a][c]["games"] for c in common) / games
        diff = sum(1 for c in common
                   if abs(data[a][c]["turns"] - data[base][c]["turns"]) > 1e-9)

        # A DARK COLUMN MUST NOT RENDER AS A NUMBER. `pay_calls` only exists in the log when the
        # act-drop audit is armed (the census forces it; a plain sweep does not), and printing
        # "0 / 0.00%" there reads as "the lever did not move payment work" -- a finding, from an
        # absent counter. Same rule the census analyzer enforces for its gate line.
        def cell(k, w):
            if b[k] == 0 and v[k] == 0:
                return f"{'n/a':>{w}} {'-':>7}"
            dd = (v[k] - b[k]) / b[k] * 100 if b[k] else float("nan")
            return f"{v[k]:>{w},} {dd:>6.2f}%"
        print(f"{a:>10} {cell('units',14)} {cell('cands',14)} {cell('pay',14)} "
              f"{aturn:>10.4f} {aturn - bturn:>+9.4f} {diff:>8} /{len(common):<4}")
    print()
    if b["pay"] == 0:
        print("NOTE: `pay_calls` is ABSENT from these logs (the ACT_HOLD_COST line needs the")
        print("act-drop audit armed) -- shown as n/a, not 0.  Re-run with MTG_ACT_DROP_AUDIT=1 to")
        print("read the uncharged payment half.")
    print("`chunks!=base` is the quality read: 0 means every chunk's mean win turn is bit-identical")
    print("to base, i.e. no game changed anywhere.  A non-zero count localises the move to those")
    print("chunks; re-run just those seeds with --log-dir to name the games.")


if __name__ == "__main__":
    main()

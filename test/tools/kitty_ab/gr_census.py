#!/usr/bin/env python3
"""Read a pooled escalation census and report, per arm PAIR, which regressions are UNRECOVERABLE.

A regression is unrecoverable when the test arm is worse than the baseline at the SHIPPED settings
AND stays worse at 100x budget AND at 100x budget + 1 depth ply. Anything that recovers at either
escalation was budget dilution or horizon, not a worse decision -- and only the survivors are the
class the no-lossy-truncation bar rejects.

Both arms are escalated at every cell (the generator emits them together), so each cell compares
like with like.

Usage: gr_census.py <play-wins-dir> <census-wins-dir> <arm> <arm> [<arm>...]
"""
import itertools
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from gen_gr_escalate_manifest import BLOCKS, CELLS   # noqa: E402

MAX_TURNS = 8


def load(path):
    out = {}
    if not path.exists():
        return out
    for line in path.read_text().splitlines():
        p = line.split()
        if len(p) >= 2:
            out[int(p[0])] = MAX_TURNS + 1 if int(p[1]) < 0 else int(p[1])
    return out


def main():
    play = pathlib.Path(sys.argv[1])
    cens = pathlib.Path(sys.argv[2])
    arms = sys.argv[3:]

    shipped = {a: {b: load(play / f"{a}.{b}.wins") for b in BLOCKS} for a in arms}
    # Census cells: one file per (arm, block, cell, gi), each holding a single game.
    esc = {a: {c[0]: {} for c in CELLS} for a in arms}
    for p in sorted(cens.glob("*.wins")):
        arm, rest = p.stem.split(".", 1)
        if arm not in esc:
            continue
        block, cell, gtag = rest.rsplit("_", 2)
        gi = int(gtag[1:])
        rows = load(p)
        if rows:
            esc[arm][cell][(block, gi)] = next(iter(rows.values()))

    for base, test in itertools.permutations(arms, 2):
        rows = []
        for block in BLOCKS:
            b, t = shipped[base][block], shipped[test][block]
            for gi in sorted(set(b) & set(t)):
                if t[gi] <= b[gi]:
                    continue                              # not a regression for `test`
                cells = {}
                survives = True
                for cell, _b, _d in CELLS:
                    bv = esc[base][cell].get((block, gi))
                    tv = esc[test][cell].get((block, gi))
                    cells[cell] = (bv, tv)
                    if bv is None or tv is None:
                        survives = False                  # missing cell -> cannot claim survival
                    elif tv <= bv:
                        survives = False                  # recovered here
                rows.append((block, gi, b[gi], t[gi], cells, survives))
        surv = [r for r in rows if r[5]]
        print(f"=== {test} vs {base}: {len(rows)} regressions, "
              f"{len(surv)} UNRECOVERABLE (worse at 100x budget AND +1 ply) ===")
        for block, gi, bw, tw, cells, s in rows:
            if not s:
                continue
            detail = "  ".join(f"{c}:{v[0]}->{v[1]}" for c, v in cells.items())
            print(f"    {block} gi={gi}  play {bw}->{tw}   {detail}")
        print()


if __name__ == "__main__":
    main()

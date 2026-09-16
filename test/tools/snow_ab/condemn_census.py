#!/usr/bin/env python3
"""Read the Snow escalation census and report, per arm PAIR, which regressions are UNRECOVERABLE.

A regression is unrecoverable when the test arm is worse than the baseline at the SHIPPED settings
AND stays worse at 100x budget AND at 100x budget + 1 depth ply. Anything that recovers at either
escalation was budget dilution or horizon, not a worse decision -- and only the survivors are the
class the no-lossy-truncation bar rejects.

Both arms are escalated at every cell (the generator emits them together), so each cell compares
like with like. A MISSING cell is never read as survival.

Usage: condemn_census.py <play-wins-dir> <census-wins-dir> <arm> <arm> [<arm>...]
"""
import itertools
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from gen_condemn_escalate_manifest import CELLS, MAX_TURNS   # noqa: E402


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
    if len(sys.argv) < 5:
        sys.exit(__doc__)
    play = pathlib.Path(sys.argv[1])
    cens = pathlib.Path(sys.argv[2])
    arms = sys.argv[3:]

    shipped = {a: load(play / f"snow__{a}.wins") for a in arms}
    esc = {a: {c[0]: {} for c in CELLS} for a in arms}
    for p in sorted(cens.glob("*.wins")):
        arm, cell, gtag = p.stem.rsplit("_", 2)
        if arm not in esc or cell not in esc[arm]:
            continue
        rows = load(p)
        if rows:
            esc[arm][cell][int(gtag[1:])] = next(iter(rows.values()))

    for base, test in itertools.permutations(arms, 2):
        rows = []
        b, t = shipped[base], shipped[test]
        for gi in sorted(set(b) & set(t)):
            if t[gi] <= b[gi]:
                continue                                  # not a regression for `test`
            cells, survives = {}, True
            for cell, _b, _d in CELLS:
                bv, tv = esc[base][cell].get(gi), esc[test][cell].get(gi)
                cells[cell] = (bv, tv)
                if bv is None or tv is None:
                    survives = False                      # missing cell -> cannot claim survival
                elif tv <= bv:
                    survives = False                      # recovered here
            rows.append((gi, b[gi], t[gi], cells, survives))
        surv = [r for r in rows if r[4]]
        print(f"=== {test} vs {base}: {len(rows)} regressions, "
              f"{len(surv)} UNRECOVERABLE (worse at 100x budget AND +1 ply) ===")
        for gi, bw, tw, cells, s in rows:
            if not s:
                continue
            detail = "  ".join(f"{c}:{v[0]}->{v[1]}" for c, v in cells.items())
            print(f"    gi={gi}  play {bw}->{tw}   {detail}")
        print()


if __name__ == "__main__":
    main()

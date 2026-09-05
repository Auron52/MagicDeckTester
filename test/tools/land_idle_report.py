#!/usr/bin/env python3
"""Read a gen_land_idle_manifest batch log and report base-vs-idle per deck, per cell, per block.

Delta is idle - base on the repo metric (avg turn-to-win, unwon scored as max_turns+1), so NEGATIVE
IS BETTER. A cell that reads exactly 0.0000 on both blocks is the rule never firing, which is the
expected and required reading for a deck whose manabase cannot present the tapped/untapped choice
(burn's drop is 100% forced) -- those are the built-in negative controls, and a nonzero there means
the rule does something other than what it claims.

The two blocks are disjoint seed sets: `train` is where a direction may be chosen, `hold` is where it
has to survive. A sign that flips between them is noise, however large either number looks alone.
"""
import collections
import pathlib
import re
import sys


def main():
    log = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "logs/land_idle/batch.log")
    rows = {}
    for m in re.finditer(r"^(\S+): played=(\d+) avg=([\d.]+) digest=(\S+)", log.read_text(), re.M):
        name, played, avg, digest = m.group(1), int(m.group(2)), float(m.group(3)), m.group(4)
        arm, deck, cell, block = name.split(".")
        rows[(arm, deck, cell, block)] = (avg, played, digest)

    cells = sorted({(d, c) for (_, d, c, _) in rows})
    blocks = ["train", "hold"]
    print(f"{'deck':<16}{'cell':<5}  " + "  ".join(f"{b:>26}" for b in blocks))
    print(f"{'':<16}{'':<5}  " + "  ".join(f"{'base':>8}{'idle':>9}{'delta':>9}" for _ in blocks))
    tot = collections.defaultdict(list)
    for deck, cell in cells:
        line = f"{deck:<16}{cell:<5}  "
        parts = []
        for b in blocks:
            base = rows.get(("base", deck, cell, b))
            idle = rows.get(("idle", deck, cell, b))
            if not base or not idle:
                parts.append(f"{'-':>26}")
                continue
            d = idle[0] - base[0]
            parts.append(f"{base[0]:>8.4f}{idle[0]:>9.4f}{d:>+9.4f}")
            if cell == "d3":
                tot[b].append(d)
        print(line + "  ".join(parts))

    print()
    for b in blocks:
        if tot[b]:
            mean = sum(tot[b]) / len(tot[b])
            worse = sum(1 for d in tot[b] if d > 0.0001)
            better = sum(1 for d in tot[b] if d < -0.0001)
            inert = sum(1 for d in tot[b] if abs(d) <= 0.0001)
            print(f"d3 suite mean [{b:<5}] {mean:+.4f}   "
                  f"better={better} worse={worse} inert(negative controls){inert}"
                  f"  n={len(tot[b])}")


if __name__ == "__main__":
    main()

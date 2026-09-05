#!/usr/bin/env python3
"""Read a gen_fluct_rebuy_manifest batch log and report the four arms per cell, per block.

Deltas are (arm - old) on the repo metric (avg turn-to-win, unwon scored as max_turns+1), so
NEGATIVE IS BETTER. Non-fluctuator decks run old-vs-new only and are the negative control: every
lever sits behind FluctuatorProvider hooks, so a nonzero delta there means a lever leaks outside
its provider (report the digest pair, not just the averages -- byte-identity is the real check).

The two blocks are disjoint seed sets: `train` is where a direction may be chosen, `hold` is where
it has to survive. A sign that flips between them is noise, however large either number looks.

Also reads the per-game .wins files (3 tokens per game: gi, win_turn, digest) from the batch's
--game-log-dir to report per-arm LOSS counts -- turn-to-win alone does not price a deck-out
(the score charges nothing for library spent; a rule that wins faster but dies more must show it).
"""
import collections
import pathlib
import re
import sys

ARMS = ["old", "new", "rebuy", "holdfuel"]


def main():
    logdir = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "logs/fluct_ab")
    log = logdir / "batch.log"
    rows = {}
    for m in re.finditer(r"^(\S+): played=(\d+) avg=([\d.]+) digest=(\S+)", log.read_text(), re.M):
        name, played, avg, digest = m.group(1), int(m.group(2)), float(m.group(3)), m.group(4)
        arm, deck, cell, block = name.split(".")
        rows[(arm, deck, cell, block)] = (avg, played, digest)

    wins = {}
    windir = logdir / "wins"
    if windir.is_dir():
        for f in windir.glob("*.wins"):
            toks = f.read_text().split()
            losses = sum(1 for i in range(len(toks) // 3) if toks[3 * i + 1] == "-1")
            wins[f.stem] = (len(toks) // 3, losses)

    blocks = ["train", "hold"]

    print("=== fluctuator: all four arms (delta = arm - old; NEGATIVE IS BETTER) ===")
    fcells = sorted({c for (a, d, c, b) in rows if d == "fluctuator"})
    hdr = f"{'cell':<5}{'block':<7}{'old':>8}" + "".join(f"{a:>9}{'d':>7}" for a in ARMS[1:]) + f"{'  losses old->new':>20}"
    print(hdr)
    for cell in fcells:
        for b in blocks:
            old = rows.get(("old", "fluctuator", cell, b))
            if not old:
                continue
            line = f"{cell:<5}{b:<7}{old[0]:>8.4f}"
            for a in ARMS[1:]:
                r = rows.get((a, "fluctuator", cell, b))
                line += f"{r[0]:>9.4f}{r[0]-old[0]:>+7.3f}" if r else f"{'-':>16}"
            lo = wins.get(f"old.fluctuator.{cell}.{b}")
            ln = wins.get(f"new.fluctuator.{cell}.{b}")
            if lo and ln:
                line += f"   {lo[1]}/{lo[0]} -> {ln[1]}/{ln[0]}"
            print(line)

    print()
    print("=== negative control (old vs new; anything nonzero = a lever leaks) ===")
    ndecks = sorted({d for (a, d, c, b) in rows if d != "fluctuator"})
    bad = 0
    for deck in ndecks:
        for cell in sorted({c for (a, d, c, b) in rows if d == deck}):
            for b in blocks:
                o = rows.get(("old", deck, cell, b))
                n = rows.get(("new", deck, cell, b))
                if not o or not n:
                    continue
                if o[2] != n[2]:
                    bad += 1
                    print(f"  LEAK {deck}.{cell}.{b}: old avg={o[0]:.4f} digest={o[2]}  new avg={n[0]:.4f} digest={n[2]}")
    if bad == 0:
        print(f"  all {len(ndecks)} decks byte-identical old vs new on both blocks (digest match)")

    print()
    print("=== per-arm fluctuator loss totals (both blocks, all cells) ===")
    tot = collections.defaultdict(lambda: [0, 0])
    for key, (n, l) in wins.items():
        parts = key.split(".")
        if len(parts) == 4 and parts[1] == "fluctuator":
            tot[parts[0]][0] += n
            tot[parts[0]][1] += l
    for a in ARMS:
        n, l = tot[a]
        if n:
            print(f"  {a:<9} losses {l}/{n}  ({100.0*l/n:.2f}%)")


if __name__ == "__main__":
    main()

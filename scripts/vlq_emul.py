#!/usr/bin/env python3
"""The EMULATED-LADDER arm of valueleaf.sh (phase E0 calibrates it, phase E measures it, E.5 may adopt it).

  vlq_emul.py sidecar <staged.value.json> <out.value.json> [--r <emul_r.json>]
      The staged sidecar in the emulated ladder: value_play.ladder = "emulated" (warm-up passes on the model,
      the committing pass on the rollout leaf at the depth the rollout ladder would reach), with the shape
      keys of other ladders dropped (leaf / alpha / commit / exhaust_mult) and the deck's play settings kept.
      Without --r: no emul_r, so the engine calibrates R per game -- phase E0's calibration arm. With --r: the
      frozen R -- the arm phase E measures and E.5 would install. Exit 1 (writes nothing) when the sidecar has
      no learned model: the emulated ladder's warm-ups need one.

  vlq_emul.py r <batch.err> <profile-path> [--min-n N]
      Prints the frozen emul_r (JSON list, index 0 = depth 1) for the deck whose PROFILE is <profile-path>,
      read from the "[rollout-stats]   emul R by depth, deck=<profile>: d1=R(n) ..." lines that MTG_ROLLOUT_STATS
      writes at exit. Depths from 1 up to the first with fewer than N samples (default 20); the engine reads
      the last entry for every deeper pass. Exit 2 when the deck has no usable depth 1 (it never calibrated).

Why a frozen R, and why measured per deck: docs/design/leaf-vs-heuristic-asymmetries-2026-10-20.md s6-7
(Landfall: per-game calibration cost 1.11x live; the frozen R 0.93x and -0.0355 turns with the model's root
tie-break). The fleet screen of 2026-10-21 found R from ~7 to ~950 units per leaf at depth 1 across decks.
"""
import json
import os
import re
import sys

SHAPE_KEYS = ("leaf", "alpha", "commit", "exhaust_mult", "emul_r")
LINE = re.compile(r"emul R by depth, deck=(.+?): (d\d.*)$")


def sidecar(argv):
    src, out = argv[0], argv[1]
    r = None
    if "--r" in argv:
        r = json.load(open(argv[argv.index("--r") + 1]))
        if not (isinstance(r, list) and r and all(isinstance(x, (int, float)) and x > 0 for x in r)):
            print(f"vlq_emul: bad emul_r {r!r}", file=sys.stderr)
            return 1
    v = json.load(open(src))
    m = v.get("eval_model")
    if not isinstance(m, dict) or not (m.get("trees") or m.get("coefs")):
        print(f"vlq_emul: {src} has no learned model -- no emulated arm", file=sys.stderr)
        return 1
    vp = v.get("value_play") or {}
    for k in SHAPE_KEYS:
        vp.pop(k, None)
    vp["ladder"] = "emulated"
    if r is not None:
        vp["emul_r"] = [round(float(x), 2) for x in r]
    v["value_play"] = vp
    json.dump(v, open(out, "w"), indent=1)
    return 0


def r_of(argv):
    err, prof = argv[0], argv[1]
    min_n = int(argv[argv.index("--min-n") + 1]) if "--min-n" in argv else 20
    want = {prof, os.path.abspath(prof)}
    tab = None
    for ln in open(err, errors="replace"):
        m = LINE.search(ln.rstrip("\n"))
        if m and (m.group(1) in want or os.path.abspath(m.group(1)) in want):
            tab = {int(d): (float(x), int(n)) for d, x, n in re.findall(r"d(\d+)=([\d.]+)\((\d+)\)", m.group(2))}
    if not tab:
        print(f"vlq_emul: no emul R line for {prof} in {err}", file=sys.stderr)
        return 2
    vec = []
    for d in range(1, 16):
        if d in tab and tab[d][1] >= min_n and tab[d][0] > 0:
            vec.append(round(tab[d][0], 2))
        else:
            break
    if not vec:
        print(f"vlq_emul: {prof}: no depth with >= {min_n} samples ({tab})", file=sys.stderr)
        return 2
    print(json.dumps(vec))
    return 0


def main(argv):
    if len(argv) >= 3 and argv[0] == "sidecar":
        return sidecar(argv[1:])
    if len(argv) >= 3 and argv[0] == "r":
        return r_of(argv[1:])
    print(__doc__)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

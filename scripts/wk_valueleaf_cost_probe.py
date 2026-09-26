#!/usr/bin/env python3
"""What would a value-leaf generation actually COST for WhiteKnights?

The question this answers is the user's: is generating a leaf cheap enough that we should just do it
and MEASURE whether the deck wants one, instead of inferring "no leaf" from shape_probe -- which only
ever compares the MODEL-LESS shapes and therefore cannot, by construction, tell us what a FITTED leaf
would do.

Cost model, read out of scripts/valueleaf.sh rather than guessed:

  PHASE A  rows      ROW_GAMES=2500 games at the deck's play settings, ROW_K=3 searched labels
  PHASE B  fit       no games
  PHASE C  matrix    MATRIX_TARGET=400 games/cell x 4 seeds (8008 9009 10010 11011)
                       H arm: depths 1..5, UNBOUNDED (budget_ms 0), value OFF, MTG_LAZY_LEAF=1
                       V arm: depths 1..8, needs the model (cannot be measured before it exists)
  PHASE C.5/D        no games
  PHASE E  A/B       AB_GAMES=1000 x 8 seeds x 2 arms, plus optional trust/play A/Bs at play settings

The H cells dominate and are the only part that is BOTH expensive and measurable up front, so that is
what this measures directly. The driver's own internal cost model (valueleaf_depth_matrix.py:836)
prices a cell as 4^depth for H and 0.02*3^depth for V -- i.e. it expects V to be ~200x cheaper at
depth 5, which is the entire point of a value leaf (an O(1) evaluator replacing the horizon rollout).
So bounding H bounds phase C.

Phase A is measured too, since 2500 games x 3 searched labels is not obviously negligible.

Everything rides ONE pooled batch (CLAUDE.md). Small game counts by design: this is a rate
measurement, not an experiment -- we want seconds/game per cell, then arithmetic.
"""
import argparse
import json
import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DECK = os.path.join(ROOT, "decks/WhiteKnights/WhiteKnights.cod")
PROF = os.path.join(ROOT, "decks/WhiteKnights/WhiteKnights.profile.json")

HDEPTHS = [1, 2, 3, 4, 5]
MATRIX_TARGET, MATRIX_SEEDS = 400, 4
ROW_GAMES, ROW_K = 2500, 3
AB_GAMES, AB_SEEDS, AB_ARMS = 1000, 8, 2


def main():
    ap = argparse.ArgumentParser()
    # Per-depth game counts: deeper cells are slower, so sample fewer. These are RATE samples.
    ap.add_argument("--games", default="40,40,40,24,12",
                    help="games per H depth 1..5 (comma-separated)")
    ap.add_argument("--rows-games", type=int, default=60, help="games for the phase-A rate sample")
    ap.add_argument("--out", default=os.path.join(ROOT, "logs/wk_vlcost"))
    args = ap.parse_args()

    per = [int(x) for x in args.games.split(",")]
    if len(per) != len(HDEPTHS):
        raise SystemExit(f"--games needs {len(HDEPTHS)} values for depths {HDEPTHS}")
    os.makedirs(args.out, exist_ok=True)
    gl = os.path.join(args.out, "gl")
    subprocess.run(["rm", "-rf", gl], check=True)
    os.makedirs(gl, exist_ok=True)

    jobs = []
    # --- the H cells, exactly as the matrix runs them: unbounded, value OFF -------------------
    for d, g in zip(HDEPTHS, per):
        jobs.append({"name": f"H{d}", "deck": DECK, "profile": PROF,
                     "games": g, "seed": 8008, "depth": d,
                     "budget_ms": 0,              # 0 = unbounded; the H arm's whole point
                     "value_model": False,        # value OFF -- the heuristic arm
                     "ignore_play_profile": True})
    # --- a phase-A-shaped cell: play settings, K searched labels per decision ------------------
    # ROW_K is the number of SEARCHED labels per decision, which is what makes a row dump cost
    # multiples of a plain game. There is no manifest key for it, so this cell measures the plain
    # play-settings game rate and the projection multiplies by ROW_K -- stated as an approximation
    # rather than dressed up as a measurement.
    jobs.append({"name": "A_playrate", "deck": DECK, "profile": PROF,
                 "games": args.rows_games, "seed": 610000, "depth": 5, "budget_ms": 20})

    mf = os.path.join(args.out, "manifest.json")
    json.dump({"jobs": jobs}, open(mf, "w"), indent=1)
    print(f"{len(jobs)} cells -> {mf}")
    print(f"H games per depth: {dict(zip(HDEPTHS, per))}; phase-A rate sample: {args.rows_games}\n")
    sys.stdout.flush()

    env = {**os.environ, "MTG_LAZY_LEAF": "1", "MTG_DUMP_UNITS": "1"}
    t0 = time.time()
    with open(os.path.join(args.out, "run.out"), "w") as fo, \
         open(os.path.join(args.out, "run.err"), "w") as fe:
        p = subprocess.Popen([os.path.join(ROOT, "build/Release/mtg"), "--batch", mf,
                              "--game-log-dir", gl], stdout=fo, stderr=subprocess.PIPE,
                             text=True, cwd=ROOT, env=env)
        for line in p.stderr:
            fe.write(line)
            if line.startswith("[batch]") or "SLOW-GAME" in line:
                print("  | " + line.rstrip(), flush=True)
        rc = p.wait()
    wall = time.time() - t0
    if rc != 0:
        raise SystemExit(f"batch failed rc={rc}; see {args.out}/run.err")
    print(f"\nprobe wall: {wall/60:.1f} min on {len(os.sched_getaffinity(0))} cores\n")

    # Per-cell CORE time. `--game-log-dir` writes only .wins and .units -- there is no per-game
    # timing file -- so the source is the batch's own per-job stdout line:
    #     <name>: played=N avg=X digest=D ms=M
    # M is the job's summed core-ms (checked against this batch's wall x cores), which is exactly
    # what a core-hour projection needs.
    job_ms = {}
    for line in open(os.path.join(args.out, "run.out")):
        if ": played=" in line and "ms=" in line:
            nm = line.split(":", 1)[0].strip()
            for tok in line.split():
                if tok.startswith("ms="):
                    try:
                        job_ms[nm] = float(tok[3:])
                    except ValueError:
                        pass

    def cell_ms(name):
        return job_ms.get(name)

    def cell_units(name):
        p2 = os.path.join(gl, name + ".units")
        if not os.path.exists(p2):
            return None
        tot = 0
        for line in open(p2):
            f = line.split()
            if len(f) > 1:
                try:
                    tot += int(f[1])
                except ValueError:
                    pass
        return tot

    print("   cell      games    units/game        s/game (core)    -> 400g x 4 seeds")
    matrix_core_h = 0.0
    rows = []
    for d, g in zip(HDEPTHS, per):
        u = cell_units(f"H{d}")
        ms = cell_ms(f"H{d}")
        spg = (ms / 1000.0 / g) if (ms and g) else None
        upg = (u / g) if (u and g) else None
        proj = (spg * MATRIX_TARGET * MATRIX_SEEDS / 3600.0) if spg else None
        if proj:
            matrix_core_h += proj
        rows.append((f"H{d}", g, upg, spg, proj))
        print(f"   {f'H{d}':<9} {g:>5}  {('%.0f' % upg) if upg else 'n/a':>12}  "
              f"{('%.3f' % spg) if spg else 'n/a':>18}  "
              f"{('%.1f core-h' % proj) if proj else 'n/a':>18}")

    ums = cell_ms("A_playrate")
    aspg = (ums / 1000.0 / args.rows_games) if ums else None
    print()
    if aspg:
        a_core_h = aspg * ROW_GAMES * ROW_K / 3600.0
        e_core_h = aspg * AB_GAMES * AB_SEEDS * AB_ARMS / 3600.0
        print(f"   phase A  : {aspg:.3f} s/game at play settings x {ROW_GAMES} games x K={ROW_K}"
              f"  -> ~{a_core_h:.1f} core-h  (K multiplier is an APPROXIMATION, see header)")
        print(f"   phase E  : same rate x {AB_GAMES} x {AB_SEEDS} seeds x {AB_ARMS} arms"
              f"          -> ~{e_core_h:.1f} core-h")
    else:
        a_core_h = e_core_h = 0.0
        print("   phase A/E: no timing file -- rate unavailable")

    cores = len(os.sched_getaffinity(0))
    print(f"\n   phase C  : H cells ~{matrix_core_h:.1f} core-h  (V cells need the model; the driver's")
    print(f"              own cost model prices V at ~200x cheaper than H at depth 5, so H dominates)")
    total = matrix_core_h + a_core_h + e_core_h
    print(f"\n   TOTAL (H-dominated floor): ~{total:.1f} core-h -> ~{total/cores:.2f} h wall at {cores} cores")
    print("   Treat as a FLOOR: it omits the V cells, phase B/D fitting, the trust/play A/Bs, and")
    print("   any load-imbalance tail. It is the right order-of-magnitude for a go/no-go decision.")


if __name__ == "__main__":
    main()

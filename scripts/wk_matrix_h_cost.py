#!/usr/bin/env python3
"""The REAL cost of a value-leaf matrix's H cells for WhiteKnights -- and a test of the lazy-leaf
"answer-identical" claim the generator relies on.

TWO THINGS AT ONCE, because they share the same cells.

(1) COST. Phase C of a value-leaf generation is MATRIX_TARGET=400 games x 4 seeds x (H1..H5 + V1..V8).
    The H cells are the expensive half and the only half measurable before a model exists. They run
    UNBOUNDED (budget_ms 0), value OFF, against a SCRATCH deck dir with NO sidecar
    (valueleaf.sh's make_variant_deck). That last detail matters: an earlier probe of mine measured
    these against the real deck dir, which now ships a leaf:none shape sidecar, and that made the
    cells ~7x cheaper than phase C would actually pay. So this moves the sidecar aside.

(2) IS MTG_LAZY_LEAF ANSWER-IDENTICAL? valueleaf.sh arms it on every H cell and describes it as
    "answer-identical (per-game win turns verified), worth -10.4% at d4 / -36.8% at d5 in units".
    An 8-game spot check at depth 5 disagreed loudly -- avg 4.875 with it on versus 4.375 with it
    off -- but 8 games is far too few to claim a defect in shared machinery, and I have misdiagnosed
    this exact lever before (the per-pass-vs-full-depth probe). So it gets a real sample, paired per
    game index, at every depth. This is load-bearing for the generator: the H ladder IS the crossover
    reference the whole leaf-adoption decision turns on ("trust the leaf" means "the leaf matches H5"),
    so if the lever moves H answers, every matrix in the repo measured a shifted reference.

ONE pooled batch over both arms and all depths (LAZY_LEAF is a heurarm slot, so it is a per-job
manifest flag rather than a process-global env var -- no per-arm split needed).
"""
import argparse
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DECK = os.path.join(ROOT, "decks/WhiteKnights/WhiteKnights.cod")
PROF = os.path.join(ROOT, "decks/WhiteKnights/WhiteKnights.profile.json")
SIDE = os.path.join(ROOT, "decks/WhiteKnights/WhiteKnights.value.json")
STASH = SIDE + ".MATRIXCOST_STASH"

HDEPTHS = [1, 2, 3, 4, 5]
MATRIX_TARGET, MATRIX_SEEDS = 400, 4


def read_wins(path):
    out = {}
    if not os.path.exists(path):
        return out
    for line in open(path):
        f = line.split()
        if len(f) >= 3:
            try:
                out[int(f[0])] = (int(f[1]), f[2])
            except ValueError:
                pass
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--games", type=int, default=200)
    ap.add_argument("--seed", type=int, default=8008)
    ap.add_argument("--out", default=os.path.join(ROOT, "logs/wk_hcost"))
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    gl = os.path.join(args.out, "gl")
    shutil.rmtree(gl, ignore_errors=True)
    os.makedirs(gl, exist_ok=True)

    jobs = []
    for d in HDEPTHS:
        for arm, lazy in (("lazy0", False), ("lazy1", True)):
            jobs.append({"name": f"H{d}_{arm}", "deck": DECK, "profile": PROF,
                         "games": args.games, "seed": args.seed,
                         "depth": d, "budget_ms": 0,       # unbounded -- the H arm's point
                         "value_model": False,             # heuristic arm
                         "ignore_play_profile": True,
                         "flags": {"MTG_LAZY_LEAF": lazy}})
    mf = os.path.join(args.out, "manifest.json")
    json.dump({"jobs": jobs}, open(mf, "w"), indent=1)
    print(f"{len(jobs)} jobs (5 depths x 2 lazy arms x {args.games} games) -> {mf}")
    print("sidecar moved aside for the run (phase C's H cells see no sidecar)\n")
    sys.stdout.flush()

    moved = False
    try:
        if os.path.exists(SIDE):
            os.rename(SIDE, STASH); moved = True
        env = {**os.environ, "MTG_DUMP_UNITS": "1"}
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
    finally:
        if moved and os.path.exists(STASH):
            os.rename(STASH, SIDE)     # always restore, including on a crash
    if rc != 0:
        raise SystemExit(f"batch failed rc={rc}; see {args.out}/run.err")

    job = {}
    for line in open(os.path.join(args.out, "run.out")):
        if ": played=" in line:
            nm = line.split(":", 1)[0].strip()
            rec = {}
            for tok in line.split():
                if "=" in tok:
                    k, v = tok.split("=", 1)
                    rec[k] = v
            job[nm] = rec

    def units(name):
        p2 = os.path.join(gl, name + ".units")
        if not os.path.exists(p2):
            return None
        t = 0
        for line in open(p2):
            f = line.split()
            if len(f) > 1:
                try:
                    t += int(f[1])
                except ValueError:
                    pass
        return t

    print(f"== matrix H-cell cost, WhiteKnights, unbounded (budget 0), no sidecar, {args.games} games\n")
    print(f"   {'cell':<6} {'lazy':<5} {'avg':>8} {'ms/game':>9} {'units/game':>12}"
          f"   {'-> 400g x 4 seeds':>18}")
    proj = {"lazy0": 0.0, "lazy1": 0.0}
    for d in HDEPTHS:
        for arm in ("lazy0", "lazy1"):
            nm = f"H{d}_{arm}"
            r = job.get(nm)
            if not r:
                print(f"   H{d:<5} {arm:<5} {'MISSING':>8}")
                continue
            g = int(r.get("played", 0)) or args.games
            mspg = float(r.get("ms", 0)) / g
            u = units(nm)
            ch = mspg / 1000.0 * MATRIX_TARGET * MATRIX_SEEDS / 3600.0
            proj[arm] += ch
            print(f"   H{d:<5} {arm:<5} {r.get('avg','?'):>8} {mspg:>9.1f} "
                  f"{(('%.0f' % (u / g)) if u else 'n/a'):>12}   {ch:>13.2f} core-h")
        print()

    # ---- the answer-identity test, paired per game index -------------------------------------
    print("   LAZY-LEAF ANSWER IDENTITY (paired per game index, lazy1 vs lazy0):\n")
    print(f"   {'cell':<6} {'identical digest':>17} {'same win turn':>15} {'better':>7} {'worse':>6}"
          f" {'d_avg':>9} {'units ratio':>12}")
    any_move = False
    for d in HDEPTHS:
        a = read_wins(os.path.join(gl, f"H{d}_lazy0.wins"))
        b = read_wins(os.path.join(gl, f"H{d}_lazy1.wins"))
        ks = sorted(set(a) & set(b))
        if not ks:
            print(f"   H{d:<5} {'MISSING':>17}")
            continue
        iden = sum(1 for k in ks if a[k][1] == b[k][1])
        same = sum(1 for k in ks if a[k][0] == b[k][0])
        better = sum(1 for k in ks if b[k][0] < a[k][0])
        worse = sum(1 for k in ks if b[k][0] > a[k][0])
        d_avg = (sum(b[k][0] for k in ks) - sum(a[k][0] for k in ks)) / len(ks)
        u0, u1 = units(f"H{d}_lazy0"), units(f"H{d}_lazy1")
        ratio = (u1 / u0) if (u0 and u1) else float("nan")
        if same != len(ks):
            any_move = True
        print(f"   H{d:<5} {iden:>9}/{len(ks):<7} {same:>8}/{len(ks):<6} {better:>7} {worse:>6}"
              f" {d_avg:>+9.4f} {ratio:>12.3f}")
    print()
    print("   'answer-identical' as valueleaf.sh describes it means same WIN TURN on every game.")
    print(f"   -> {'VIOLATED at >=1 depth' if any_move else 'HOLDS at every depth'}")
    print()
    print(f"   phase C H-cell projection: lazy0 ~{proj['lazy0']:.1f} core-h, "
          f"lazy1 ~{proj['lazy1']:.1f} core-h")
    cores = len(os.sched_getaffinity(0))
    print(f"   (V1..V8 need the model and cannot be measured yet; the driver's own scheduler prices")
    print(f"    V at 0.02*3^d against H's 4^d, i.e. ~200x cheaper at depth 5, so H dominates phase C)")
    print(f"   at {cores} cores that is ~{proj['lazy1']/cores*60:.0f} min wall for the H half.")


if __name__ == "__main__":
    main()

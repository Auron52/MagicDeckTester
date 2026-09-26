#!/usr/bin/env python3
"""Is the FITTED value leaf better than the adopted LEAFLESS shape for WhiteKnights?

WHY THIS EXISTS RATHER THAN JUST READING PHASE E.

valueleaf.sh's phase E A/Bs `staged` (the new model) against `live` (whatever sidecar the deck
currently ships). To generate at all, the deck's adopted `leaf: "none"` sidecar had to be moved aside
-- otherwise `phase_train` copies the live sidecar and only adds `eval_model`, so `leaf: "none"`
survives into the staged file, AttachValueSidecar swaps the fitted trees for Constant(), and every V
cell plus phase E measures the leafless shape while reporting on "the leaf". So during generation
`live` is BARE (the plain rollout ladder), and phase E therefore answers

    "is the fitted leaf better than the plain heuristic ladder?"

which is not the question. The deck does not ship the plain ladder -- it ships the leafless escalation.
The decision is

    "is the fitted leaf better than the LEAFLESS shape we adopted?"

and that is a three-way comparison this script runs on the SIDECAR route (two scratch deck dirs
differing only in their value.json), over every configuration the deck is played at, paired per game.

ARMS (each is a real deck directory, which is how production resolves a sidecar):
  bare      no value.json          -- the plain rollout ladder, the common control
  leafless  the adopted sidecar    -- leaf:none + alpha:relaxed  (what ships today)
  leaf      the generated sidecar  -- eval_model + whatever value_play phase D derived

Reads the same axes as the shape screen: deterministic search UNITS and the paired sign test on
per-game win turns. ONE pooled batch over all arms and configs.
"""
import argparse
import json
import math
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DECKDIR = os.path.join(ROOT, "decks/WhiteKnights")
STEM = "WhiteKnights"

CONFIGS = [("d0b0", 0, 0), ("d3b10", 3, 10), ("d3b20", 3, 20), ("d5b20", 5, 20), ("d5b40", 5, 40)]


def make_variant(dest, sidecar):
    """A deck dir identical to the real one except for its value.json (siblings symlinked).

    Mirrors valueleaf.sh's make_variant_deck. `sidecar=None` means NO sidecar, which is the live arm
    for a deck that ships none -- placing an empty file instead would silently activate the hybrid,
    because sidecar PRESENCE is the activation mechanism.
    """
    shutil.rmtree(dest, ignore_errors=True)
    os.makedirs(dest, exist_ok=True)
    for f in os.listdir(DECKDIR):
        if f == f"{STEM}.value.json":
            continue
        os.symlink(os.path.realpath(os.path.join(DECKDIR, f)), os.path.join(dest, f))
    if sidecar and os.path.exists(sidecar) and os.path.getsize(sidecar) > 0:
        shutil.copy(sidecar, os.path.join(dest, f"{STEM}.value.json"))


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
    ap.add_argument("--leafless", default=os.path.join(ROOT, "logs/wk_valueleaf/adopted_leafnone.value.json"))
    ap.add_argument("--leaf", default=os.path.join(ROOT, f"logs/eval/{STEM}.value.STAGED.json"))
    ap.add_argument("--games", type=int, default=500)
    ap.add_argument("--seeds", type=int, default=8)
    ap.add_argument("--seed0", type=int, default=3310000)
    ap.add_argument("--max-turns", type=int, default=8)
    ap.add_argument("--out", default=os.path.join(ROOT, "logs/wk_leafcmp"))
    args = ap.parse_args()

    for p, what in ((args.leafless, "adopted leafless sidecar"), (args.leaf, "generated staged model")):
        if not os.path.exists(p):
            raise SystemExit(f"missing {what}: {p}")
    lf = json.load(open(args.leaf))
    if not lf.get("eval_model"):
        raise SystemExit(f"{args.leaf} carries NO eval_model -- nothing to compare")
    vp = lf.get("value_play") or {}
    if vp.get("leaf") == "none":
        raise SystemExit(
            f"{args.leaf} has value_play.leaf == 'none', which makes AttachValueSidecar substitute\n"
            "Constant() and IGNORE the fitted trees. The staged model inherited the leafless shape;\n"
            "regenerate with the adopted sidecar moved aside.")
    print(f"staged model: {len(json.dumps(lf['eval_model']))} bytes of model, value_play={json.dumps(vp)}\n")

    os.makedirs(args.out, exist_ok=True)
    vroot = os.path.join(args.out, "decks")
    arms = {"bare": None, "leafless": args.leafless, "leaf": args.leaf}
    for arm, side in arms.items():
        make_variant(os.path.join(vroot, arm), side)

    gl = os.path.join(args.out, "gl")
    shutil.rmtree(gl, ignore_errors=True)
    os.makedirs(gl, exist_ok=True)

    jobs = []
    for tag, d, b in CONFIGS:
        for arm in arms:
            dd = os.path.join(vroot, arm)
            for s in range(args.seeds):
                jobs.append({"name": f"{arm}_{tag}_s{s}",
                             "deck": os.path.join(dd, f"{STEM}.cod"),
                             "profile": os.path.join(dd, f"{STEM}.profile.json"),
                             "games": args.games, "seed": args.seed0 + s * 1000,
                             "depth": d, "budget_ms": b, "max_turns": args.max_turns})
    mf = os.path.join(args.out, "manifest.json")
    json.dump({"jobs": jobs}, open(mf, "w"), indent=1)
    print(f"{len(jobs)} jobs (3 arms x {len(CONFIGS)} configs x {args.seeds} seeds x {args.games} games "
          f"= {len(jobs)*args.games:,} games) -> {mf}")
    sys.stdout.flush()

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
    if rc != 0:
        raise SystemExit(f"batch failed rc={rc}; see {args.out}/run.err")

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

    def gather(arm, tag):
        w, u = {}, 0
        for s in range(args.seeds):
            nm = f"{arm}_{tag}_s{s}"
            for gi, v in read_wins(os.path.join(gl, nm + ".wins")).items():
                w[(s, gi)] = v
            u += units(nm) or 0
        return w, u

    def cmp(a, b):
        """b vs a, paired. -> (better, worse, ident, d_avg, z, n)"""
        ks = sorted(set(a) & set(b))
        better = sum(1 for k in ks if b[k][0] < a[k][0])
        worse = sum(1 for k in ks if b[k][0] > a[k][0])
        ident = sum(1 for k in ks if b[k][1] == a[k][1])
        d = (sum(b[k][0] for k in ks) - sum(a[k][0] for k in ks)) / len(ks) if ks else 0.0
        n = better + worse
        return better, worse, ident, d, ((better - worse) / math.sqrt(n) if n else 0.0), len(ks)

    print(f"\n== fitted leaf vs adopted leafless, WhiteKnights, {args.seeds} x {args.games} games/cell")
    print(f"   seeds {args.seed0}+ (disjoint from the shape screen's 5,510,000 and repro 7,730,000)\n")
    print(f"   {'config':<7} {'comparison':<22} {'units':>8} {'better':>7} {'worse':>6} {'ident':>11} {'d_avg':>9} {'z':>6}")
    verdict = {}
    for tag, _d, _b in CONFIGS:
        wb, ub = gather("bare", tag)
        wn, un = gather("leafless", tag)
        wl, ul = gather("leaf", tag)
        for label, (wa, ua), (wc, uc) in (
                ("leafless vs bare", (wb, ub), (wn, un)),
                ("leaf vs bare", (wb, ub), (wl, ul)),
                ("leaf vs LEAFLESS", (wn, un), (wl, ul))):
            bet, wor, ide, d, z, n = cmp(wa, wc)
            ratio = (uc / ua) if ua else None
            rs = f"{ratio:8.2f}" if ratio else f"{'n/a':>8}"
            print(f"   {tag:<7} {label:<22} {rs} {bet:>7} {wor:>6} {ide:>6}/{n:<4} {d:>+9.4f} {z:>+6.1f}")
            if label == "leaf vs LEAFLESS":
                verdict[tag] = (bet, wor, d, z, ratio)
        print()

    print("   VERDICT on the question that matters (leaf vs the shape we actually ship):\n")
    bad = [t for t, (b, w, d, z, r) in verdict.items() if z < -1.0]
    good = [t for t, (b, w, d, z, r) in verdict.items() if z > 2.0]
    for t, (b, w, d, z, r) in verdict.items():
        call = "leaf BETTER" if z > 2.0 else ("leaf WORSE" if z < -1.0 else "no sign")
        print(f"     {t:<7} {b} better / {w} worse, z {z:+.1f}, d_avg {d:+.4f}, "
              f"units {('%.2fx' % r) if r else 'n/a'}   -> {call}")
    print()
    if good and not bad:
        print("   The leaf wins somewhere and loses nowhere -> the 'no leaf' call was WRONG; adopt it.")
    elif bad and not good:
        print("   The leaf is worse where it differs and better nowhere -> 'no leaf' CONFIRMED by")
        print("   direct measurement, not inferred from a model-less probe.")
    else:
        print("   Mixed or no sign -> a TRADE at best. Report, do not adopt.")


if __name__ == "__main__":
    main()

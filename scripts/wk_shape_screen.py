#!/usr/bin/env python3
"""Multi-configuration search-shape screen for WhiteKnights (the ADOPTION-grade version).

WHY THIS EXISTS, given scripts/shape_probe.py already ran.

`shape_probe` is the CHEAP EARLY TEST: one configuration (the deck's d5/b20), enough to answer "does
this deck want a value leaf at all?" before committing hours to generating one. For WhiteKnights it
answered clearly -- `leaf: "none"`, i.e. do not generate -- and that is all it is for.

It is NOT enough to ADOPT a shape. Every `leaf: none` / `ladder: single` adoption already in this repo
cleared a much higher bar, and the bar is written into those decks' own sidecar notes:

  Minotaur       d5b20 19/2 z+3.71, d5b40 13/1 z+3.21, d3b10 0/0, d3b20 0/0, d0b0 byte-identical
  Dragons        d5b20, d5b40, d3b10, d3b20, d0b0 byte-identical, then RE-CONFIRMED on a 2nd seed base
  StompySurprise d6b20, d5b20, d5b40, d3b20, d3b10, d0b0 + reproduced on a disjoint seed base
  Goblins        d6b40, d5b40, d5b20, d3b20, d3b10, d0b0 + reproduced on seeds 7,000,000+

Adopting off ONE cell at z+2.2 would also fall into a trap this repo has already paid for twice:

  * "never classify a bundle by its cleanest part" -- `fit_nl` bundles TWO decisions (`leaf: none`
    AND `ladder: single`). In the probe they had IDENTICAL quality (both -0.0013, 5 better/0 worse,
    z+2.2) and differed only in cost (0.21x vs 0.17x), so the quality evidence belongs to `leaf:
    none` alone and `ladder: single` is a pure cost claim. This screen reports them separately so
    each is adopted on its own evidence.
  * "replicate trades before ruling" -- a small effect is exactly where selection bias bites, which
    is why the disjoint-seed reproduction below is not optional.

So this runs the full grid: 3 arms x 5 configurations x 8 seed blocks, plus a reproduction of the
headline configuration on a DISJOINT seed base. ONE pooled `mtg --batch` over all of it (CLAUDE.md:
never a batch per arm or per cell -- that is the "waves are a loop" defect that cost 23 hours once).

d0b0 is in the grid for a specific reason and is not padding: at depth 0 there is no search, so all
three arms MUST be byte-identical. If they are not, the arms are not what they claim to be and every
other row is void -- it is the screen's own control.

The arm definitions are IMPORTED from shape_probe rather than retyped, so the thing being adopted
cannot drift from the thing that was screened.

Usage: python3 scripts/wk_shape_screen.py [--games 500] [--seeds 8]
"""
import argparse
import importlib.util
import json
import math
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _load_shape_probe():
    """Import shape_probe.py for its ARMS/SHAPE_FOR tables (it is a script, not a module)."""
    spec = importlib.util.spec_from_file_location(
        "shape_probe", os.path.join(ROOT, "scripts", "shape_probe.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


SP = _load_shape_probe()
ARMS = SP.ARMS                 # heur (control, first) / esc_nl / fit_nl
SHAPE_FOR = SP.SHAPE_FOR

# (tag, depth, budget_ms). d0b0 first because it is the control, not a result.
CONFIGS = [
    ("d0b0",  0,  0),
    ("d3b10", 3, 10),
    ("d3b20", 3, 20),
    ("d5b20", 5, 20),   # the deck's shipped configuration, and the headline row
    ("d5b40", 5, 40),
]
HEADLINE = "d5b20"


def read_wins(path):
    """-> {game_index: (win_turn, digest)}. The .wins line is `<gi> <win_turn> <digest>`."""
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


def units_of(gl, job):
    """Deterministic search units for a job.

    Units are NOT on stdout -- `MTG_DUMP_UNITS=1` makes the batch write a per-job `<name>.units`
    file of `<game_index> <units>` beside the `.wins` files, and the job's total is the sum. Same
    source shape_probe reads, deliberately: a second parser for the same number is a second thing
    that can disagree with the screen it is supposed to corroborate.
    """
    path = os.path.join(gl, job + ".units")
    if not os.path.exists(path):
        return None
    total = 0
    for line in open(path):
        f = line.split()
        if len(f) > 1:
            try:
                total += int(f[1])
            except ValueError:
                pass
    return total


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--games", type=int, default=500)
    ap.add_argument("--seeds", type=int, default=8)
    ap.add_argument("--max-turns", type=int, default=8)
    # Disjoint from shape_probe's 880000 base, so the reproduction is a genuinely fresh sample
    # rather than a re-read of the games that produced the candidate.
    ap.add_argument("--seed0", type=int, default=5510000)
    ap.add_argument("--repro-seed0", type=int, default=7730000)
    ap.add_argument("--out", default=os.path.join(ROOT, "logs/wk_shape/screen"))
    args = ap.parse_args()

    deck = os.path.join(ROOT, "decks/WhiteKnights/WhiteKnights.cod")
    prof = os.path.join(ROOT, "decks/WhiteKnights/WhiteKnights.profile.json")
    if not os.path.exists(prof):
        raise SystemExit("no profile -- a shape screen without the deck's profile measures a deck we do not ship")
    os.makedirs(args.out, exist_ok=True)
    gl = os.path.join(args.out, "gl")
    subprocess.run(["rm", "-rf", gl], check=True)
    os.makedirs(gl, exist_ok=True)

    jobs = []

    def add(block, tag, depth, budget, seed0):
        for arm, keys in ARMS.items():
            for b in range(args.seeds):
                jobs.append({
                    "name": f"{block}_{arm}_{tag}_s{b}",
                    "deck": deck, "profile": prof,
                    "games": args.games, "seed": seed0 + b * 1000,
                    "depth": depth, "budget_ms": budget,
                    "max_turns": args.max_turns,
                    # Same discipline as shape_probe: ignore the deck's own play settings so the
                    # control cannot leak a shape into itself, and write every shape key explicitly.
                    "ignore_play_profile": True,
                    **keys,
                })

    for tag, d, b in CONFIGS:
        add("main", tag, d, b, args.seed0)
    # Reproduction of the headline cell only, on a disjoint seed base.
    hd = next((d, b) for t, d, b in CONFIGS if t == HEADLINE)
    add("repro", HEADLINE, hd[0], hd[1], args.repro_seed0)

    mf = os.path.join(args.out, "manifest.json")
    json.dump({"jobs": jobs}, open(mf, "w"), indent=1)
    print(f"{len(jobs)} jobs ({len(ARMS)} arms x {len(CONFIGS)} configs + repro x {args.seeds} seeds "
          f"x {args.games} games = {len(jobs) * args.games:,} games) -> {mf}")
    sys.stdout.flush()

    # MTG_DUMP_UNITS=1 is what produces the per-job `.units` files the cost axis is read from.
    # Without it the screen still reports the sign test but every ratio comes back NaN.
    env = {**os.environ, "MTG_DUMP_UNITS": "1"}
    run_out_path = os.path.join(args.out, "run.out")
    with open(run_out_path, "w") as fo, open(os.path.join(args.out, "run.err"), "w") as fe:
        p = subprocess.Popen([os.path.join(ROOT, "build/Release/mtg"), "--batch", mf,
                              "--game-log-dir", gl], stdout=fo, stderr=subprocess.PIPE,
                             text=True, cwd=ROOT, env=env)
        for line in p.stderr:
            fe.write(line)
            # Surface utilisation live: CLAUDE.md requires checking this in the first ten minutes.
            if line.startswith("[batch]") or "SLOW-GAME" in line:
                print("  | " + line.rstrip(), flush=True)
        rc = p.wait()
    if rc != 0:
        raise SystemExit(f"batch failed rc={rc}; see {args.out}/run.err")

    print(f"\n== WhiteKnights shape screen -- {args.seeds} x {args.games} games per cell, "
          f"arms vs the plain rollout ladder (heur)")
    print("   Paired per GAME on identical (seed, game index). z = net / sqrt(better + worse).\n")
    hdr = f"   {'config':<7} {'arm':<7} {'units':>12} {'ratio':>7} {'d_avg':>9} {'better':>7} {'worse':>6} {'ident':>7} {'z':>6}"
    print(hdr)

    verdicts = {}
    for tag, _d, _b in CONFIGS + [("repro:" + HEADLINE, 0, 0)]:
        block = "main"
        ctag = tag
        if tag.startswith("repro:"):
            block, ctag = "repro", tag.split(":", 1)[1]
        base = {}
        for b in range(args.seeds):
            for gi, v in read_wins(os.path.join(gl, f"{block}_heur_{ctag}_s{b}.wins")).items():
                base[(b, gi)] = v
        base_units = sum(filter(None, (units_of(gl, f"{block}_heur_{ctag}_s{b}")
                                       for b in range(args.seeds)))) or None
        for arm in ARMS:
            cur = {}
            for b in range(args.seeds):
                for gi, v in read_wins(os.path.join(gl, f"{block}_{arm}_{ctag}_s{b}.wins")).items():
                    cur[(b, gi)] = v
            keys = sorted(set(base) & set(cur))
            if not keys:
                print(f"   {tag:<7} {arm:<7} {'MISSING':>12}")
                continue
            better = sum(1 for k in keys if cur[k][0] < base[k][0])
            worse = sum(1 for k in keys if cur[k][0] > base[k][0])
            ident = sum(1 for k in keys if cur[k][1] == base[k][1])
            d_avg = (sum(cur[k][0] for k in keys) - sum(base[k][0] for k in keys)) / len(keys)
            n = better + worse
            z = (better - worse) / math.sqrt(n) if n else 0.0
            u = sum(filter(None, (units_of(gl, f"{block}_{arm}_{ctag}_s{b}")
                                  for b in range(args.seeds)))) or None
            # d0b0 does no search at all, so every arm's units are 0 and a RATIO IS UNDEFINED there
            # -- print "n/a" rather than nan. d0b0 earns its place as the byte-identity control, not
            # as a cost row, and a nan in an adoption table is exactly the sort of cell that gets
            # skimmed as a number.
            ratio = (u / base_units) if (u and base_units) else None
            rs = f"{ratio:7.2f}" if ratio is not None else f"{'n/a':>7}"
            print(f"   {tag:<7} {arm:<7} {u if u else 0:>12} {rs} {d_avg:>+9.4f} "
                  f"{better:>7} {worse:>6} {ident:>6}/{len(keys)} {z:>+6.1f}")
            verdicts[(tag, arm)] = dict(better=better, worse=worse, ident=ident, n=len(keys),
                                        d_avg=d_avg, z=z, ratio=ratio)
        print()

    # ---- the control: at depth 0 there is no search, so every arm must be byte-identical --------
    print("   CONTROL (d0b0, no search -- all arms MUST be byte-identical):")
    ok = True
    for arm in ARMS:
        if arm == "heur":
            continue
        v = verdicts.get(("d0b0", arm))
        if not v:
            print(f"     {arm}: MISSING"); ok = False; continue
        good = v["ident"] == v["n"] and v["better"] == 0 and v["worse"] == 0
        ok = ok and good
        print(f"     {arm}: {v['ident']}/{v['n']} identical digests "
              f"-> {'OK' if good else 'FAILED -- the arms are not what they claim; every other row is void'}")
    print(f"   control: {'PASS' if ok else 'FAIL'}\n")

    # ---- separate the bundle: leaf:none (esc_nl) vs +ladder:single (fit_nl) ---------------------
    print("   SEPARATING THE BUNDLE (the quality claim belongs to leaf:none; ladder:single is cost):")
    for tag in [t for t, _, _ in CONFIGS] + ["repro:" + HEADLINE]:
        e, f = verdicts.get((tag, "esc_nl")), verdicts.get((tag, "fit_nl"))
        if not e or not f:
            continue
        same_q = (e["better"], e["worse"]) == (f["better"], f["worse"])
        er = f"{e['ratio']:.2f}x" if e["ratio"] is not None else "n/a"
        fr = f"{f['ratio']:.2f}x" if f["ratio"] is not None else "n/a"
        print(f"     {tag:<7} leaf:none {e['better']}/{e['worse']} z{e['z']:+.1f} at {er}"
              f"   |  +ladder:single {f['better']}/{f['worse']} z{f['z']:+.1f} at {fr}"
              f"   {'(quality IDENTICAL -> single is a pure cost win)' if same_q else '(quality DIFFERS -> judge separately)'}")
    print()
    print("   ADOPTION RULE (docs/design/per-deck-search-shape.md): a clean win is no regression on")
    print("   ANY axis -- units <= 1.00x and the sign test not negative -- at EVERY configuration the")
    print("   deck is played at, reproduced on a disjoint seed base. Anything else is a TRADE and is")
    print("   the user's call, not an agent's.")


if __name__ == "__main__":
    main()

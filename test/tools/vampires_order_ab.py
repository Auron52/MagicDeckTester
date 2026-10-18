#!/usr/bin/env python3
"""Vampires: the AI-drafted cast order + discard buckets, measured in ONE pooled `mtg --batch` queue.

docs/design/vampires-cast-order-2026-10-18.md, section 5. Both levers are USER-OWNED and ship DEFAULT
OFF; this is the measurement the USER's decision takes as input. It is NOT run by the drafting agent:
the binary still carries the convoke / payer defects A-L of docs/design/vampires/claude_sweep_1.md, and
defect B (convoke ignores creatures cast earlier in the same plan) is precisely what makes the order's
one load-bearing slot -- Markov Baron after the bodies -- observable at all. Run it after those fixes
land, on the merged tree.

ARMS (per-job "flags", heurarm slots, so every arm shares ONE queue and one tail -- CLAUDE.md "WAVES ARE
A LOOP"; each arm pins BOTH levers explicitly, so an inherited environment cannot leak into a job):
    off      MTG_VAMPIRES_ORDER=0 MTG_VAMPIRES_BUCKET_DISCARD=0   (what the deck plays today)
    order    MTG_VAMPIRES_ORDER=1 MTG_VAMPIRES_BUCKET_DISCARD=0
    buckets  MTG_VAMPIRES_ORDER=0 MTG_VAMPIRES_BUCKET_DISCARD=1
    both     MTG_VAMPIRES_ORDER=1 MTG_VAMPIRES_BUCKET_DISCARD=1

CELLS (the deck's play settings; the profile carries no value_play, so depth / budget are pinned):
    std d3 b10, std d5 b20, 2hg d3 b10, 2hg d5 b20; --with-d0 adds std d0 (the greedy runner, where a
    rank is read most directly; cheap).
SEEDS: two disjoint held-out blocks (A / B) far from every suite seed (smoke 1001, regression
2002/3003, overnight 4004-10010) and from the earlier cast-order A/B blocks (11,700,001+), spaced
wider than any job, so the sign split across blocks is a real reproduction check.

Usage:
    vampires_order_ab.py manifest [--scale X] [--with-d0] > build/scratch/vampires-order-ab/manifest.json
    MTG_PGO=0 build/Release/mtg --batch build/scratch/vampires-order-ab/manifest.json --threads 20 \\
        --game-log-dir build/scratch/vampires-order-ab/wins 2> build/scratch/vampires-order-ab/batch.err \\
        | tee build/scratch/vampires-order-ab/batch.log
    vampires_order_ab.py report build/scratch/vampires-order-ab/wins [--max-turns 8]

Watch the `[batch] heartbeat: N/M workers busy` line in batch.err in the first ten minutes (CLAUDE.md).
The report pairs every arm against `off` PER GAME (same cell, block and global game index), prints
faster / slower / net / mean / se / t / changed-digest per cell and per block, and lists every SLOWER
game with its single-game repro (the adoption bar: every slower game must recover at --depth 8
--budget-ms 0, and every cell's aggregate must be net <= 0 -- "+1 is not neutral. 0 is neutral.").
"""
import collections
import json
import pathlib
import re
import sys
from math import sqrt

DECK = "decks/Vampires/Vampire.cod"
PROFILE = "decks/Vampires/Vampire.profile.json"

ARMS = {
    "off":     {"MTG_VAMPIRES_ORDER": False, "MTG_VAMPIRES_BUCKET_DISCARD": False},
    "order":   {"MTG_VAMPIRES_ORDER": True,  "MTG_VAMPIRES_BUCKET_DISCARD": False},
    "buckets": {"MTG_VAMPIRES_ORDER": False, "MTG_VAMPIRES_BUCKET_DISCARD": True},
    "both":    {"MTG_VAMPIRES_ORDER": True,  "MTG_VAMPIRES_BUCKET_DISCARD": True},
}
BASELINE = "off"

BLOCKS = {"A": 12_180_001, "B": 12_480_001}

# (format, depth, budget_ms, games PER BLOCK PER ARM). Sized off measured per-game core cost (d3 b10
# ~1.2 core-s on the pre-fix binary, 2026-10-18 liveness batch; the ledger's d5 b40 ~1.3; 2HG dearer):
# at scale 1 the queue is 4 arms x 2 blocks x (1500 + 750 + 500 + 250) = 24,000 games, ~10-15
# core-hours, ~30-45 min at 20 threads. Re-project off the first heartbeat, not off this note.
CELLS = [
    ("std", 3, 10, 1500),
    ("std", 5, 20, 750),
    ("2hg", 3, 10, 500),
    ("2hg", 5, 20, 250),
]
D0_CELL = ("std", 0, 0, 5000)

FORMAT_FIELDS = {"std": {}, "2hg": {"starting_life": 30, "opponent_heads": 2}}

JOB_RE = re.compile(r"^(?P<arm>[a-z]+)\.(?P<fmt>std|2hg)\.d(?P<depth>\d+)\.b(?P<budget>\d+)\.(?P<block>[A-Z])$")


def job_name(arm, fmt, depth, budget, block):
    return f"{arm}.{fmt}.d{depth}.b{budget}.{block}"


def manifest(argv):
    scale = 1.0
    cells = list(CELLS)
    i = 0
    while i < len(argv):
        if argv[i] == "--scale":
            scale = float(argv[i + 1])
            i += 2
            continue
        if argv[i] == "--with-d0":
            cells.append(D0_CELL)
            i += 1
            continue
        sys.exit(f"unknown argument {argv[i]!r}")
    jobs = []
    for fmt, depth, budget, games in cells:
        n = max(1, int(round(games * scale)))
        for block, seed in BLOCKS.items():
            for arm, flags in ARMS.items():
                job = {
                    "name": job_name(arm, fmt, depth, budget, block),
                    "deck": DECK,
                    "profile": PROFILE,
                    "games": n,
                    "seed": seed,
                    "depth": depth,
                    "budget_ms": budget,
                    "flags": dict(flags),
                    # the deeper cell first, so the tail is not one d5 job started last
                    "weight": depth,
                }
                job.update(FORMAT_FIELDS[fmt])
                jobs.append(job)
    json.dump({"jobs": jobs}, sys.stdout, indent=1)
    sys.stdout.write("\n")
    total = sum(j["games"] for j in jobs)
    print(f"[vampires_order_ab] {len(jobs)} jobs, {total} games", file=sys.stderr)


def load_wins(path, max_turns):
    """-> {global_game_index: (win_turn, digest)}; an unwon game (-1) scores max_turns + 1."""
    out = {}
    for line in path.read_text().splitlines():
        parts = line.split()
        if len(parts) < 2:
            continue
        gi, wt = int(parts[0]), int(parts[1])
        out[gi] = (max_turns + 1 if wt < 0 else wt, parts[2] if len(parts) > 2 else "")
    return out


def paired(base, arm):
    keys = sorted(set(base) & set(arm))
    if not keys:
        return None
    d = [arm[k][0] - base[k][0] for k in keys]
    n = len(d)
    mean = sum(d) / n
    se = sqrt(sum((x - mean) ** 2 for x in d) / (n - 1) / n) if n > 1 else float("nan")
    return {
        "n": n,
        "net": sum(d),
        "mean": mean,
        "se": se,
        "t": (mean / se) if se and se == se and se > 0 else 0.0,
        "faster": sum(1 for x in d if x < 0),
        "slower": [k for k, x in zip(keys, d) if x > 0],
        "dig": sum(1 for k in keys if base[k][1] and base[k][1] != arm[k][1]),
        "base_avg": sum(base[k][0] for k in keys) / n,
        "arm_avg": sum(arm[k][0] for k in keys) / n,
        "unpaired": len(set(base) ^ set(arm)),
    }


def repro(arm, fmt, depth, budget, block, gi):
    """The single-game repro of global game `gi` of a block (analyze-deck 5e step 2)."""
    seed = BLOCKS[block] + gi
    env = " ".join(f"{k}={1 if v else 0}" for k, v in ARMS[arm].items())
    fmt_env = "MTG_START_LIFE=30 MTG_OPPONENT_HEADS=2 " if fmt == "2hg" else ""
    return (f"{fmt_env}{env} MTG_PGO=0 build/Release/mtg {DECK} --profile {PROFILE} --seed {seed} "
            f"--game-index {gi % 10} --games 1 --depth {depth} --budget-ms {budget} --threads 1")


def report(argv):
    max_turns = 8
    args = []
    for a in argv:
        if a.startswith("--max-turns"):
            max_turns = int(a.split("=", 1)[1])
        else:
            args.append(a)
    if not args:
        sys.exit("usage: vampires_order_ab.py report <wins-dir> [--max-turns=N]")
    root = pathlib.Path(args[0])
    # (fmt, depth, budget) -> block -> arm -> wins
    runs = collections.defaultdict(lambda: collections.defaultdict(dict))
    for p in sorted(root.glob("*.wins")):
        m = JOB_RE.match(p.stem)
        if not m:
            continue
        key = (m["fmt"], int(m["depth"]), int(m["budget"]))
        runs[key][m["block"]][m["arm"]] = load_wins(p, max_turns)
    if not runs:
        sys.exit(f"no <arm>.<fmt>.d<D>.b<B>.<block>.wins under {root}")
    verdict_ok = True
    for key in sorted(runs):
        fmt, depth, budget = key
        print(f"\n=== {fmt} d{depth} b{budget} ===")
        print(f"{'arm':8s} {'block':5s} {'n':>6s} {'faster':>6s} {'slower':>6s} {'net':>6s} "
              f"{'mean':>9s} {'se':>8s} {'t':>7s} {'dig':>6s}  off->arm avg")
        for arm in [a for a in ARMS if a != BASELINE]:
            pooled_base, pooled_arm = {}, {}
            for block in sorted(runs[key]):
                arms = runs[key][block]
                if BASELINE not in arms or arm not in arms:
                    continue
                r = paired(arms[BASELINE], arms[arm])
                if r is None:
                    continue
                for gi in arms[BASELINE]:
                    pooled_base[(block, gi)] = arms[BASELINE][gi]
                for gi in arms[arm]:
                    pooled_arm[(block, gi)] = arms[arm][gi]
                print(f"{arm:8s} {block:5s} {r['n']:6d} {r['faster']:6d} {len(r['slower']):6d} {r['net']:6d} "
                      f"{r['mean']:+9.5f} {r['se']:8.5f} {r['t']:+7.2f} {r['dig']:6d}  "
                      f"{r['base_avg']:.4f} -> {r['arm_avg']:.4f}"
                      + (f"  UNPAIRED={r['unpaired']}" if r["unpaired"] else ""))
            r = paired(pooled_base, pooled_arm)
            if r is None:
                continue
            flag = "" if r["net"] <= 0 else "   <-- NET WORSE (fails the bar)"
            if r["net"] > 0:
                verdict_ok = False
            print(f"{arm:8s} {'ALL':5s} {r['n']:6d} {r['faster']:6d} {len(r['slower']):6d} {r['net']:6d} "
                  f"{r['mean']:+9.5f} {r['se']:8.5f} {r['t']:+7.2f} {r['dig']:6d}  "
                  f"{r['base_avg']:.4f} -> {r['arm_avg']:.4f}{flag}")
            for block, gi in r["slower"][:40]:
                b = pooled_base[(block, gi)][0]
                a = pooled_arm[(block, gi)][0]
                print(f"    slower {arm} {block} gi={gi} T{b} -> T{a}   repro: "
                      f"{repro(arm, fmt, depth, budget, block, gi)}")
            if len(r["slower"]) > 40:
                print(f"    ... and {len(r['slower']) - 40} more slower games")
    print("\nVERDICT (aggregate half of the bar only; every slower game still needs its d8 b0 recovery check): "
          + ("every cell net <= 0" if verdict_ok else "at least one cell is NET WORSE"))


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in ("manifest", "report"):
        sys.exit(__doc__)
    if sys.argv[1] == "manifest":
        manifest(sys.argv[2:])
    else:
        report(sys.argv[2:])


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""WHY one arm beats another -- the mechanism behind a screen delta, not just the delta.

A `deck_compare` screen records only a per-game OUTCOME (`[win] job=<tag> gi=<N> wt=<T>`). That is
enough to locate every game where two lists diverge and by how much, but it cannot say what the
better list actually DID. This script closes that gap after the fact:

  1. parse the screen's stderr into per-arm {game_index: win_turn}
  2. pair two arms and rank games by the win-turn gap
  3. replay the most divergent games for BOTH arms with `--log-dir`, reusing the EXACT inputs the
     screen used (that arm's decklist + numbering, the pooled profile, seed, max_turns, life)
  4. verify each replay reproduces the recorded win turn before anything is read from it

Step 4 is not optional. The screen searches under a 20 ms/decision BUDGET on a 32-thread box, so a
replay is only faithful if it lands on the same result; a silently-diverging replay would produce a
confident, wrong story about a card. Games that fail to reproduce are reported and DROPPED, never
analysed.

Usage:
  why_card.py --err <screen.err> --manifest <screen.manifest.json> --a <tag> --b <tag>
              [--n 8] [--life 30] [--outdir logs/why/<name>]
"""
import argparse, collections, json, os, re, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MTG = ROOT / "build" / "Release" / "mtg"


def parse_wins(err_path, max_turns):
    """Per-arm {gi: win_turn}. wt=-1 (unwon) scores max_turns+1, matching deck_compare."""
    got = collections.defaultdict(dict)
    pat = re.compile(r"\[win\] job=(\S+) gi=(\d+) wt=(-?\d+)")
    for line in open(err_path, errors="replace"):
        m = pat.match(line.strip())
        if m:
            wt = int(m.group(3))
            got[m.group(1)][int(m.group(2))] = max_turns + 1 if wt < 0 else wt
    return got


def jobs_by_name(manifest):
    return {j["name"]: j for j in json.load(open(manifest))["jobs"]}


def replay_all(jobs, picks, tags, outdir, life, threads):
    """Replay every (arm, game) pair as ONE pooled `mtg --batch`, through the SAME code path the
    screen used -- the job dict is copied VERBATIM and only the game selection is narrowed. That is
    why fidelity is by construction rather than by reconstructing a CLI invocation: profile,
    value_profile, ladder_value_leaf, max_turns and the depth/budget policy all travel untouched.

    Game selection follows the engine's own repro convention (BatchRunner: `global_gi =
    job.game_index + wi.game`, seed = `job.seed + wi.game`): to re-run global game N alone, set
    game_index=N, games=1 and seed=base+N, so wi.game=0 reproduces both the index and the seed.

    One batch, not a loop of invocations -- a loop would strand cores on each invocation's tail.
    -> {(tag, gi): trace_path}
    """
    outdir.mkdir(parents=True, exist_ok=True)
    trace_dir = outdir / "traces"
    mini = {"jobs": []}
    for tag in tags:
        base = jobs[tag]
        for _, gi in picks:
            j = dict(base)
            j["name"] = tag
            j["games"] = 1
            j["game_index"] = gi
            j["seed"] = int(base["seed"]) + gi
            if life:
                j["starting_life"] = life
            mini["jobs"].append(j)
    mpath = outdir / "replay.manifest.json"
    mpath.write_text(json.dumps(mini, indent=1))
    cmd = [str(MTG), "--batch", str(mpath), "--game-trace-dir", str(trace_dir)]
    if threads:
        cmd += ["--threads", str(threads)]
    env = dict(os.environ)
    if life:
        env["MTG_START_LIFE"] = str(life)
    print(f"  one pooled batch: {len(mini['jobs'])} single-game jobs -> {trace_dir}")
    p = subprocess.run(cmd, cwd=ROOT, env=env, capture_output=True, text=True)
    if p.returncode != 0:
        sys.stderr.write(p.stdout[-3000:] + p.stderr[-3000:])
        sys.exit(f"replay batch failed rc={p.returncode}")
    return {(tag, gi): trace_dir / f"{tag}_gi{gi}.json" for tag in tags for _, gi in picks}


def trace_win_turn(path, max_turns):
    """The win turn a trace actually records, scored the way deck_compare scores it."""
    if not path or not path.exists():
        return None
    t = json.load(open(path))
    r = t.get("result") or {}
    return r.get("turn") if r.get("winner") == "player" else max_turns + 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--err", required=True)
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--a", required=True, help="arm expected to be BETTER")
    ap.add_argument("--b", required=True, help="arm expected to be WORSE")
    ap.add_argument("--n", type=int, default=8, help="games to replay from each tail")
    ap.add_argument("--life", type=int, default=0)
    ap.add_argument("--outdir", default=None)
    ap.add_argument("--threads", type=int, default=0)
    ap.add_argument("--sample", choices=("stratified", "extreme", "random"), default="stratified",
                    help="stratified (default) spreads picks across gap sizes; extreme takes the\nbiggest gaps, which selects for draw luck rather than for the card under test")
    ap.add_argument("--census-only", action="store_true",
                    help="report the divergence distribution and pick games; replay nothing")
    args = ap.parse_args()

    jobs = jobs_by_name(args.manifest)
    for t in (args.a, args.b):
        if t not in jobs:
            sys.exit(f"no such arm '{t}' in the manifest. have: {', '.join(sorted(jobs))}")
    max_turns = int(jobs[args.a].get("max_turns") or 0)
    wins = parse_wins(args.err, max_turns)
    A, B = wins.get(args.a, {}), wins.get(args.b, {})
    common = sorted(set(A) & set(B))
    if not common:
        sys.exit("no paired games -- is this the right stderr for this manifest?")

    diff = {gi: A[gi] - B[gi] for gi in common}          # negative = A won SOONER = A better
    a_better = sorted((gi for gi in common if diff[gi] < 0), key=lambda g: diff[g])
    b_better = sorted((gi for gi in common if diff[gi] > 0), key=lambda g: -diff[g])
    same = len(common) - len(a_better) - len(b_better)

    print(f"{args.a}  vs  {args.b}      {len(common):,} paired games, max_turns {max_turns}"
          + (f", life {args.life}" if args.life else ""))
    print(f"  identical win turn : {same:,}  ({same/len(common):6.1%})")
    print(f"  {args.a} sooner    : {len(a_better):,}  ({len(a_better)/len(common):6.1%})")
    print(f"  {args.b} sooner    : {len(b_better):,}  ({len(b_better)/len(common):6.1%})")
    print(f"  mean win turn      : {args.a} {sum(A[g] for g in common)/len(common):.4f}   "
          f"{args.b} {sum(B[g] for g in common)/len(common):.4f}   "
          f"delta {sum(diff.values())/len(common):+.4f}")
    hist = collections.Counter(diff.values())
    print("  gap histogram (A-B):  " + "  ".join(
        f"{k:+d}:{hist[k]:,}" for k in sorted(hist) if hist[k]))

    # SELECTION MATTERS. Taking the largest gaps selects games where the two DECKLISTS dealt very
    # different hands -- the swapped slots move every later library position -- so the extreme tail
    # is dominated by draw luck and often does not even involve the card under test. Stratified
    # sampling spreads the picks across gap sizes in proportion to how common each gap is, which is
    # what makes the per-card census below representative of the effect rather than of its tail.
    def strat(pool, n):
        if len(pool) <= n:
            return list(pool)
        by = collections.defaultdict(list)
        for gi in pool:
            by[diff[gi]].append(gi)
        out, ks = [], sorted(by, key=lambda k: -len(by[k]))
        for k in ks:                                  # proportional, at least one per gap size
            out += by[k][:max(1, round(n * len(by[k]) / len(pool)))]
        return sorted(out, key=lambda g: abs(diff[g]), reverse=True)[:n]

    if args.sample == "random":
        # An UNBIASED sample of ALL paired games, divergent or not. Required for any CONDITIONAL
        # claim ("in games where card X was cast, ..."): both the extreme and stratified pools are
        # drawn from divergent games only and are balanced across the two tails by construction, so
        # a conditional mean computed on them is selected, not estimated. Deterministic stride, so
        # the sample is reproducible without a RNG.
        step = max(1, len(common) // max(1, args.n * 2))
        picks = [("RANDOM", gi) for gi in common[::step][:args.n * 2]]
    else:
        pick_a = (a_better[:args.n] if args.sample == "extreme" else strat(a_better, args.n))
        pick_b = (b_better[:args.n] if args.sample == "extreme" else strat(b_better, args.n))
        picks = [("A_BETTER", gi) for gi in pick_a] + [("B_BETTER", gi) for gi in pick_b]
    print("\n  games picked for replay:")
    for side, gi in picks:
        print(f"    {side:9} gi={gi:<6} {args.a} wt={A[gi]:<3} {args.b} wt={B[gi]:<3} gap {diff[gi]:+d}")
    if args.census_only:
        return

    outdir = Path(args.outdir or (ROOT / "logs" / "why" / f"{args.a}__vs__{args.b}"))
    tags = (args.a, args.b)
    print(f"\n  replaying {len(picks)*len(tags)} games into {outdir} (both arms, verifying each)")
    traces = replay_all(jobs, picks, tags, outdir, args.life, args.threads)

    index, bad = [], 0
    for side, gi in picks:
        row = {"side": side, "gi": gi, "recorded": {args.a: A[gi], args.b: B[gi]}, "logs": {}}
        for tag in tags:
            path = traces[(tag, gi)]
            wt = trace_win_turn(path, max_turns)
            rec = wins[tag][gi]
            ok = wt is not None and wt == rec
            row["logs"][tag] = {"path": str(path), "replay_wt": wt,
                                "recorded_wt": rec, "reproduced": ok}
            if not ok:
                bad += 1
                print(f"    !! gi={gi:<6} {tag:22} replay wt={wt} but screen recorded {rec} -- DROPPED")
        row["reproduced"] = all(v["reproduced"] for v in row["logs"].values())
        index.append(row)

    good = [r for r in index if r["reproduced"]]
    (outdir / "index.json").write_text(json.dumps(
        {"a": args.a, "b": args.b, "life": args.life, "max_turns": max_turns,
         "paired": len(common), "games": index}, indent=1))
    print(f"\n  {len(good)}/{len(index)} game pairs reproduced faithfully"
          + (f"  ({bad} replay mismatches dropped)" if bad else ""))
    print(f"  index: {outdir/'index.json'}")


if __name__ == "__main__":
    main()

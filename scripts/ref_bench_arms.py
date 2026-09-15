#!/usr/bin/env python3
"""ref_bench, but N lever ARMS in ONE pooled batch -- a per-reference A/B of heurarm levers.

    scripts/ref_bench_arms.py --deck eldrazidisplacerflicker --log-root logs/ref_bench_edf/lift \
        --arm off MTG_EDF_EXACT_EXECUTOR=0 --arm on MTG_EDF_EXACT_EXECUTOR=1
    (an arm may also carry `budget_ms=N` -- a per-arm virtual-ms search budget for a ladder)

Every arm plays every reference game from the SAME forced opening hand (ref_bench's
`force_mulligan`), and the arms are INTERLEAVED per reference in the manifest so the pool has one
tail, not one per arm (the repo's pooling rule). Levers are the manifest's per-job `flags` block
(ai/HeuristicArm.h slots), so ONE binary runs every arm; an env var that is not a heurarm slot is
rejected by the batch runner, which is the right failure -- it means that lever cannot be A/B'd in
a pool at all.

Prints one row per reference with the human's recorded win turn and one column per arm, then the
mean / short count per arm, then the per-arm digests that MOVED against the first arm (a changed
line with the same win turn is worth knowing, and it is not a shortfall).
"""
import argparse, json, os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import deck_registry
import ref_bench


def parse_arm(spec):
    """['on', 'MTG_X=1', 'MTG_Y=0'] -> ('on', {'MTG_X': True, 'MTG_Y': False})"""
    name, flags, budget = spec[0], {}, None
    for kv in spec[1:]:
        k, _, v = kv.partition("=")
        if k == "budget_ms":                 # a per-arm search budget (virtual ms), not a lever
            budget = int(v)
            continue
        if v not in ("0", "1"):
            sys.exit("arm %s: lever %s must be =0 or =1 (got %r)" % (name, k, v))
        flags[k] = (v == "1")
    return name, flags, budget


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--deck", required=True)
    ap.add_argument("--arm", nargs="+", action="append", required=True, metavar="NAME LEVER=0/1",
                    help="an arm: a name then zero or more LEVER=0|1 (repeatable)")
    ap.add_argument("--max-turns", type=int, default=deck_registry.MAX_TURNS)
    ap.add_argument("--threads", type=int, default=0)
    ap.add_argument("--log-root", required=True, help="per-game traces + manifest + stdout go here")
    ap.add_argument("--stats", action="store_true",
                    help="MTG_ROLLOUT_STATS=1 on the batch (deterministic work counters in stdout)")
    args = ap.parse_args()
    arms = [parse_arm(a) for a in args.arm]
    names = [a[0] for a in arms]
    if len(set(names)) != len(names):
        sys.exit("arm names must be unique: %s" % names)

    decks = deck_registry.discover()
    slug = deck_registry.slug(args.deck)
    pairs = [p for p in ref_bench.ref_dirs("references") if p[0] == slug]
    if not pairs:
        sys.exit("no references for deck slug %r" % slug)
    key = deck_registry.reference_deck_key(slug)
    entry = ref_bench.collect_deck(key, pairs[0][1], decks[key], args)
    games = entry["games"]

    jobs = []
    for g in games:                      # reference-major, arm-minor: no arm-major barrier
        mull = g["mull"]
        for name, flags, budget in arms:
            j = {"name": "%s__%s" % (name, ref_bench.job_name(key, g)),
                 "deck": entry["deck_obj"].deck_file, "profile": entry["deck_obj"].profile,
                 "games": 1, "seed": g["seed"], "game_index": g["gi"],
                 "max_turns": args.max_turns,
                 "force_mulligan": "%d:%s" % (mull.get("count", 0),
                                              ",".join(str(x) for x in (mull.get("bottom") or [])))}
            if flags:
                j["flags"] = flags
            if budget is not None:
                j["budget_ms"] = budget
            jobs.append(j)

    os.makedirs(args.log_root, exist_ok=True)
    mpath = os.path.join(args.log_root, "manifest.json")
    with open(mpath, "w") as fh:
        json.dump({"jobs": jobs}, fh, indent=1)
    cmd = [ref_bench.MTG, "--batch", mpath, "--game-trace-dir", args.log_root]
    if args.threads:
        cmd += ["--threads", str(args.threads)]
    env = dict(os.environ)
    if args.stats:
        env["MTG_ROLLOUT_STATS"] = "1"
    print("pooled batch: %d jobs = %d references x %d arms (%s) in ONE process"
          % (len(jobs), len(games), len(arms), ", ".join(names)))
    sys.stdout.flush()
    p = subprocess.run(cmd, capture_output=True, text=True, env=env)
    with open(os.path.join(args.log_root, "batch.stdout"), "w") as fh:
        fh.write(p.stdout)
    with open(os.path.join(args.log_root, "batch.stderr"), "w") as fh:
        fh.write(p.stderr)
    if p.returncode != 0:
        sys.stderr.write(p.stdout[-3000:] + p.stderr[-3000:])
        sys.exit("pooled batch failed rc=%d" % p.returncode)

    LOSS = args.max_turns + 1
    wins, digests = {}, {}
    for line in p.stdout.splitlines():
        m = ref_bench.JOB_RE.match(line.strip())
        if not m:
            continue
        played, v = int(m.group(2)), float(m.group(3))
        wins[m.group(1)] = "ERR" if played == 0 else (None if v >= LOSS - 1e-9 else int(round(v)))
        dm = [t for t in line.split() if t.startswith("digest=")]
        if dm:
            digests[m.group(1)] = dm[0][7:]

    lp = lambda v: v if isinstance(v, int) else LOSS
    cell = lambda v: str(v) if isinstance(v, int) else (v if isinstance(v, str) else "NO-WIN")
    w = max(6, max(len(n) for n in names))
    print("\n=== %s  (n=%d, max_turns=%d, no-win scores %d)" % (key, len(games), args.max_turns, LOSS))
    print("%-22s %5s | %s" % ("reference", "human", " ".join("%*s" % (w, n) for n in names)))
    short = {n: 0 for n in names}
    tot = {n: 0 for n in names}
    moved = {n: [] for n in names}
    for g in games:
        base = ref_bench.job_name(key, g)
        row = []
        for n in names:
            v = wins.get("%s__%s" % (n, base), "ERR")
            tot[n] += lp(v)
            if g["human"] is not None and (v is None or (isinstance(v, int) and v > g["human"])):
                short[n] += 1
            mark = ""
            if n != names[0]:
                d0, d1 = digests.get("%s__%s" % (names[0], base)), digests.get("%s__%s" % (n, base))
                if d0 and d1 and d0 != d1:
                    mark = "*"
                    moved[n].append(g["name"])
            row.append("%*s" % (w, cell(v) + mark))
        print("%-22s %5s | %s" % (g["name"][:-5], cell(g["human"]), " ".join(row)))
    n_g = len(games)
    print("%-22s %5.3f | %s" % ("MEAN", sum(lp(g["human"]) for g in games) / n_g,
                                " ".join("%*.3f" % (w, tot[n] / n_g) for n in names)))
    print("%-22s %5s | %s" % ("short", "--", " ".join("%*d" % (w, short[n]) for n in names)))
    for n in names[1:]:
        if moved[n]:
            print("digest moved vs %s under %s: %s" % (names[0], n, " ".join(moved[n])))
    print("\nlogs: %s" % args.log_root)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Deck-average A/B of heurarm levers: N arms x chunked seeds, ONE pooled batch, PAIRED by chunk.

    scripts/deck_avg_arms.py --deck decks/X/X.cod --log-root logs/x/avg \
        --seeds 3001 3061 --games 50 --chunk 10 --max-turns 12 \
        --arm off MTG_LEVER=0 --arm on MTG_LEVER=1

Every arm plays the SAME games (seed s+c, game_index c, games=chunk for each chunk offset c), so the
per-chunk delta is paired and draw-order luck cancels. Chunks are interleaved arm-minor in the
manifest so the pool has ONE tail (the arm-major starvation lesson). Seeds are spaced by `games`
within a seed base, so no two chunks replay the same permutation. Levers ride the manifest's
per-job `flags` block, so one binary runs every arm.

Prints the per-chunk table, each arm's mean, the paired delta of every arm against the first, the
count of chunks better/worse/tied, and the summed job wall (ms). With --stats the batch runs under
MTG_ROLLOUT_STATS=1 so its stdout also carries deterministic work counters.
"""
import argparse, json, os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import deck_registry
import ref_bench


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--deck", required=True, help="decklist path (profile resolved beside it)")
    ap.add_argument("--profile", default=None)
    ap.add_argument("--arm", nargs="+", action="append", required=True, metavar="NAME LEVER=0/1")
    ap.add_argument("--seeds", nargs="+", type=int, default=[3001, 3061])
    ap.add_argument("--games", type=int, default=50, help="games per seed base")
    ap.add_argument("--chunk", type=int, default=10, help="games per job")
    ap.add_argument("--max-turns", type=int, default=12)
    ap.add_argument("--threads", type=int, default=0)
    ap.add_argument("--log-root", required=True)
    ap.add_argument("--stats", action="store_true")
    args = ap.parse_args()
    arms = [(a[0], dict((kv.partition("=")[0], kv.partition("=")[2] == "1") for kv in a[1:]))
            for a in args.arm]
    names = [a[0] for a in arms]
    profile = args.profile
    if profile is None:
        stem = os.path.splitext(args.deck)[0]
        profile = stem + ".profile.json"
        if not os.path.exists(profile):
            sys.exit("no profile beside %s; pass --profile" % args.deck)

    chunks = [(s, c) for s in args.seeds for c in range(0, args.games, args.chunk)]
    jobs = []
    for s, c in chunks:
        for name, flags in arms:
            j = {"name": "%s__s%d_c%d" % (name, s, c), "deck": args.deck, "profile": profile,
                 "games": args.chunk, "seed": s + c, "game_index": c, "max_turns": args.max_turns}
            if flags:
                j["flags"] = flags
            jobs.append(j)
    os.makedirs(args.log_root, exist_ok=True)
    mpath = os.path.join(args.log_root, "manifest.json")
    with open(mpath, "w") as fh:
        json.dump({"jobs": jobs}, fh, indent=1)
    cmd = [ref_bench.MTG, "--batch", mpath]
    if args.threads:
        cmd += ["--threads", str(args.threads)]
    env = dict(os.environ)
    if args.stats:
        env["MTG_ROLLOUT_STATS"] = "1"
    print("pooled batch: %d jobs = %d chunks x %d arms (%s), %d games/arm, max_turns %d"
          % (len(jobs), len(chunks), len(arms), ", ".join(names),
             len(chunks) * args.chunk, args.max_turns))
    sys.stdout.flush()
    p = subprocess.run(cmd, capture_output=True, text=True, env=env)
    open(os.path.join(args.log_root, "batch.stdout"), "w").write(p.stdout)
    open(os.path.join(args.log_root, "batch.stderr"), "w").write(p.stderr)
    if p.returncode != 0:
        sys.stderr.write(p.stdout[-3000:] + p.stderr[-3000:])
        sys.exit("pooled batch failed rc=%d" % p.returncode)

    avg, ms = {}, {}
    for line in p.stdout.splitlines():
        m = ref_bench.JOB_RE.match(line.strip())
        if not m:
            continue
        avg[m.group(1)] = float(m.group(3))
        for t in line.split():
            if t.startswith("ms="):
                ms[m.group(1)] = int(t[3:])
    w = max(7, max(len(n) for n in names))
    print("\n%-12s | %s" % ("chunk", " ".join("%*s" % (w, n) for n in names)))
    tot = {n: 0.0 for n in names}
    wall = {n: 0 for n in names}
    delta = {n: [] for n in names}
    for s, c in chunks:
        row = []
        for n in names:
            k = "%s__s%d_c%d" % (n, s, c)
            v = avg.get(k, float("nan"))
            tot[n] += v
            wall[n] += ms.get(k, 0)
            if n != names[0]:
                delta[n].append(v - avg.get("%s__s%d_c%d" % (names[0], s, c), float("nan")))
            row.append("%*.2f" % (w, v))
        print("%-12s | %s" % ("%d+%d" % (s, c), " ".join(row)))
    nc = len(chunks)
    print("%-12s | %s" % ("MEAN", " ".join("%*.4f" % (w, tot[n] / nc) for n in names)))
    print("%-12s | %s" % ("wall s", " ".join("%*d" % (w, wall[n] // 1000) for n in names)))
    for n in names[1:]:
        d = delta[n]
        better = sum(1 for x in d if x < -1e-9)
        worse = sum(1 for x in d if x > 1e-9)
        mean = sum(d) / len(d)
        sd = (sum((x - mean) ** 2 for x in d) / max(1, len(d) - 1)) ** 0.5
        t = mean / (sd / len(d) ** 0.5) if sd > 0 else float("inf")
        print("%s vs %s: paired delta %+.4f t (chunks better %d / worse %d / tied %d; paired t=%.2f); "
              "wall %+.1f%%" % (n, names[0], mean, better, worse, len(d) - better - worse, t,
                                100.0 * (wall[n] - wall[names[0]]) / max(1, wall[names[0]])))
    print("\nlogs: %s" % args.log_root)


if __name__ == "__main__":
    main()

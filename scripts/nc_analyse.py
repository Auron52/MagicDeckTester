#!/usr/bin/env python3
"""Does the Mirrorwing trick-suite result survive without CLAIRVOYANCE?

The engine's default deep search reads the TRUE library order. Every number in the
overnight campaign was measured under that. The user asked (2026-08-19) for "a smaller
number of non-clairvoyant runs to see if that changes anything" -- the concern being that
an effect can be an artifact of the search knowing what it will draw.

Baseline is free: the NC runs use seed 980000, the overnight campaign's seed, so the
overnight's first N games ARE the paired clairvoyant arm on the SAME games.

A game with no [win] line did not win by max_turns; it scores max_turns+1 (the same
convention the overnight analyser uses), so a policy that wins less often is penalised.
"""
import sys, os, re, json, glob, collections, statistics as st

MAXT = 8
ARMS = ("base", "trick", "libonly")
PAIRS = (("base", "libonly"), ("base", "trick"), ("libonly", "trick"))


def from_winlines(path, n):
    """arm -> {gi: win_turn}, defaulting every unlisted game to MAXT+1 (a loss).

    REFUSES a truncated run. "No win line" and "the job never ran" are indistinguishable in
    this file format, and treating the second as the first is not a small error: an OOM kill
    (2026-08-19: mtg reached 23 GB, the box's whole RAM, and the kernel killed it mid-batch)
    left two of three arms with zero games, which scored as a perfect 9.0000 every game and
    printed as a t=+216 result. A run that did not finish must fail loudly, not read as a
    catastrophic effect.
    """
    out = {a: {gi: MAXT + 1 for gi in range(n)} for a in ARMS}
    seen = collections.Counter()
    pat = re.compile(rb"^\[win\] job=(\S+) gi=(\d+) wt=(\d+)")
    with open(path, "rb") as fh:
        for line in fh:
            m = pat.match(line)
            if m:
                arm = m.group(1).decode()
                gi = int(m.group(2))
                if arm in out and gi < n:
                    out[arm][gi] = int(m.group(3))
                    seen[arm] += 1
    dead = [a for a in ARMS if seen[a] == 0]
    if dead:
        raise SystemExit(f"REFUSING {path}: arms {dead} have ZERO win lines -- the batch did not "
                         f"finish (check for an OOM kill). Per-arm wins: {dict(seen)}")
    lo, hi = min(seen.values()), max(seen.values())
    if lo < 0.5 * hi:
        raise SystemExit(f"REFUSING {path}: arm win counts are wildly uneven {dict(seen)} -- "
                         f"the batch looks truncated.")
    return out


def from_traces(tdir, n):
    out = {}
    for a in ARMS:
        d = {gi: MAXT + 1 for gi in range(n)}
        for p in glob.glob(os.path.join(tdir, f"{a}_gi*.json")):
            gi = int(os.path.basename(p).rsplit("_gi", 1)[1][:-5])
            if gi >= n:
                continue
            t = (json.load(open(p)).get("result") or {}).get("turn", -1)
            d[gi] = MAXT + 1 if (t is None or t < 0 or t > MAXT) else t
        out[a] = d
    return out


def paired(w, A, B, n):
    d = [w[B][gi] - w[A][gi] for gi in range(n)]
    m = st.mean(d)
    se = st.stdev(d) / len(d) ** .5
    same = sum(1 for x in d if x == 0)
    return m, se, (m / se if se else 0.0), 100.0 * (1 - same / len(d))


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 20000
    configs = collections.OrderedDict()
    # The overnight batch's own [win] lines carry the same win turns as its traces and parse
    # ~100x faster (one 180k-line file vs 60,000 JSON opens); traces are the fallback.
    ov = "logs/overnight/batch/batch.err"
    configs["clairvoyant (overnight, same seed/games)"] = (
        from_winlines(ov, n) if os.path.exists(ov) else from_traces("logs/overnight/traces", n))
    # A config whose run was truncated is SKIPPED WITH A NOTICE, not silently dropped and not
    # fatal -- one dead arm in one config must not take the whole report down with it.
    skipped = []
    for tag, path in (("honest (draw-decoupled 1-sample)", "logs/nc/batch/honest.err"),
                      ("non-clairvoyant (K=4, depth=1)", "logs/nc/batch/k4d1.err"),
                      ("non-clairvoyant (K=2, depth=1)", "logs/nc/batch/nc2.err")):
        if not os.path.exists(path):
            continue
        try:
            configs[tag] = from_winlines(path, n)
        except SystemExit as e:
            skipped.append(f"{tag}: {e}")

    print("# Non-clairvoyant check: does the trick-suite result survive?\n")
    print(f"- games per arm: {n:,}   seed 980000   max_turns {MAXT}")
    print("- apparatus: the SAME pooled keep tables as the overnight campaign (only the PLAY "
          "policy changes)")
    print("- a game with no win by max_turns scores 9\n")
    print("Delta is (second arm - first arm) in win turns; **negative = the second arm wins sooner**.\n")

    for A, B in PAIRS:
        print(f"\n## {A} vs {B}\n")
        print(f"| play policy | {A} | {B} | delta | se | t | diverged |")
        print("|---|---:|---:|---:|---:|---:|---:|")
        for tag, w in configs.items():
            m, se, t, dv = paired(w, A, B, n)
            ma = st.mean([w[A][gi] for gi in range(n)])
            mb = st.mean([w[B][gi] for gi in range(n)])
            print(f"| {tag} | {ma:.4f} | {mb:.4f} | **{m:+.4f}** | {se:.4f} | {t:+.2f} | {dv:.1f}% |")

    if skipped:
        print("\n## SKIPPED (run did not finish -- NOT a result)\n")
        for m in skipped:
            print(f"- {m}")
    print("\n## How to read this\n")
    print("The question is whether the SIGN and rough SIZE hold, not whether the numbers match. "
          "A non-clairvoyant policy plays worse in absolute terms (every arm's mean win turn "
          "rises), so only the paired delta is comparable across rows. An effect that survives "
          "here is a property of the DECK; one that collapses was the search exploiting knowledge "
          "of the library.")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""NAME-KEYED ground truth for the Goblins tutor: the real win turn of fetching each CARD.

Why another truth script (test/goblins_tutor_truth.py already exists): that one probes by RANK
(MTG_TUTOR_FORCE_RANK), so its table is invalidated the moment the ranking changes -- rank 4 is a
different card before and after. This one probes by NAME (MTG_TUTOR_FORCE_CARD), which makes the
table MODEL-INDEPENDENT: build it once, then score any number of candidate ranking models against it
without re-running a single forced game.

That matters because the model is about to be rewritten. The tempo-model project (docs/design/
goblins-enabler-worse-games.md round 13) needs a metric that is not the loss-penalized turn-unit sum,
which this session showed can be dominated by wall-clock churn and by unwon/won flips worth 99 points
each. Run at --budget-ms 0 (unbounded) so every number is DETERMINISTIC and load-independent.

Two phases:

  1. SCAN    -- run the shipped config over a sample of games; keep the ones where a searched tutor
                fetch actually happened (in the rest every arm is identical, so they carry no signal).
  2. PROBE   -- for each kept game, force each candidate card in turn and record the win turn.

The headline number is REGRET: shipped_turn - oracle_turn, i.e. how much win-turn the current ranking
leaves on the table. A model rewrite is only worth doing if that number is meaningfully above zero.

Scoring a NEW model afterwards costs one run per game, not one per card: run it, see which card it
commits to, look the card up in the table (--score).

Usage:
    python3 test/goblins_tutor_truth_table.py --seeds 4004 --games 400 --out /tmp/truth.json
    python3 test/goblins_tutor_truth_table.py --score /tmp/truth.json            # shipped model
    MTG_GOBLIN_X=1 python3 test/goblins_tutor_truth_table.py --score /tmp/truth.json
"""
import argparse
import concurrent.futures
import glob
import json
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "build/Release/mtg")
DECK, PROF = "decks/Goblins/Goblins.cod", "decks/Goblins/Goblins.profile.json"

# Every Goblin creature Matron can fetch (decks/Goblins/Goblins.cod).
CANDIDATES = [
    "Muxus, Goblin Grandee", "Rundvelt Hordemaster", "Twinshot Sniper", "Siege-Gang Commander",
    "Goblin Lackey", "Goblin Piledriver", "Goblin Matron", "Goblin King", "Goblin Chieftain",
    "Mogg War Marshal", "Goblin Warchief", "Skirk Prospector", "Goblin Chainwhirler",
    "Stingscourger", "Krenko, Mob Boss", "Pashalik Mons",
]


def run(base, gi, depth, env_extra):
    """(win_turn or None, first searched-tutor card or None) for one configuration."""
    tmp = tempfile.mkdtemp(prefix="truth_")
    try:
        env = dict(os.environ, **env_extra)
        p = subprocess.run(
            [BIN, DECK, "--profile", PROF, "--games", "1", "--seed", str(base + gi),
             "--game-index", str(gi), "--depth", str(depth), "--budget-ms", "0",
             "--ignore-play-profile", "--log-dir", tmp],
            cwd=ROOT, capture_output=True, text=True, env=env)
        turn = None
        for ln in p.stdout.splitlines():
            if ln.startswith("avg (turns)"):
                turn = int(float(ln.split(":")[1].split()[0]))
        card = None
        f = glob.glob(os.path.join(tmp, "*.json"))
        if f:
            for seg in json.load(open(f[0])).get("turns", []):
                for a in seg.get("actions", []):
                    if a.get("type") == "REVEAL" and "searched" in (a.get("source") or ""):
                        looked = a.get("lookedAt") or []
                        if looked:
                            card = looked[0].get("cardName")
                        break
                if card:
                    break
        return turn, card
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def build(args):
    seeds = [int(s) for s in args.seeds.split(",")]
    pool = concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs)

    scan = [(b, g) for b in seeds for g in range(args.games)]
    print(f"[1/2] scanning {len(scan)} games for a searched tutor fetch ...", file=sys.stderr)
    scanned = list(pool.map(lambda k: run(k[0], k[1], args.depth, {}), scan))
    kept = [(b, g, t, c) for (b, g), (t, c) in zip(scan, scanned) if c and t]
    print(f"      {len(kept)} of {len(scan)} games cast a searched tutor", file=sys.stderr)

    jobs = [(b, g, card) for (b, g, _, _) in kept for card in CANDIDATES]
    print(f"[2/2] probing {len(jobs)} (game, card) pairs ...", file=sys.stderr)
    probed = list(pool.map(lambda k: run(k[0], k[1], args.depth,
                                         {"MTG_TUTOR_FORCE_CARD": k[2]}), jobs))

    table = {}
    for (b, g, card), (t, c) in zip(jobs, probed):
        key = f"{b}:{g}"
        rec = table.setdefault(key, {"seed": b, "gi": g, "cards": {}})
        # Only count it if the forced card was really fetched; otherwise it was not a legal
        # candidate in that game (already drawn, or no Matron resolved on that line).
        if c == card and t:
            rec["cards"][card] = t
    for (b, g, t, c) in kept:
        rec = table.get(f"{b}:{g}")
        if rec is not None:
            rec["shipped_turn"], rec["shipped_card"] = t, c

    table = {k: v for k, v in table.items() if v["cards"] and "shipped_turn" in v}
    json.dump({"depth": args.depth, "games": table}, open(args.out, "w"), indent=1)
    print(f"wrote {args.out}: {len(table)} games", file=sys.stderr)
    # MUST be in the same order report() iterates (sorted), not dict insertion order -- pairing the
    # picks with the wrong games silently reports a plausible but wrong regret (it read +22 / 94.3%
    # where the truth is +4 / 99.1%, and invented 58 "pick not probeable" skips out of the mismatch).
    report(table, [(v["shipped_card"], v["shipped_turn"]) for _, v in sorted(table.items())],
           "SHIPPED")


def report(table, picks, label):
    """Regret must be FORCED-vs-FORCED to be consistent.

    Forcing collapses the tutor axis to one card, so a forced run explores fewer plans than a free
    one -- the same card can win a turn LATER under force. Comparing a model's free run against a
    forced oracle therefore yields negative "regret", which is an artifact of the instrument, not a
    model beating the oracle. So the model is scored by looking its CHOSEN CARD up in the forced
    table; a game whose chosen card was not probeable is skipped rather than mixed in.
    """
    reg_sum = n = exact = skipped = 0
    dist = {}
    for (key, rec), (card, turn) in zip(sorted(table.items()), picks):
        got = rec["cards"].get(card)
        if got is None:
            skipped += 1
            continue
        r = got - min(rec["cards"].values())
        reg_sum += r
        n += 1
        exact += (r == 0)
        dist[r] = dist.get(r, 0) + 1
    print(f"\n=== {label} vs oracle ({n} scored, {skipped} skipped: pick not probeable) ===")
    print(f"  mean regret      {reg_sum / max(1, n):+.4f} turns/game   (total {reg_sum:+d})")
    print(f"  optimal picks    {exact}/{n} = {100.0 * exact / max(1, n):.1f}%")
    print("  regret histogram " + "  ".join(f"{k:+d}:{v}" for k, v in sorted(dist.items())))
    return reg_sum, n


def score(args):
    blob = json.load(open(args.score))
    table, depth = blob["games"], blob["depth"]
    keys = sorted(table.items())
    pool = concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs)
    res = list(pool.map(lambda kv: run(kv[1]["seed"], kv[1]["gi"], depth, {}), keys))
    report(table, [(c, t) for t, c in res], args.label or "MODEL")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seeds", default="4004")
    ap.add_argument("--games", type=int, default=400)
    ap.add_argument("--depth", type=int, default=3)
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 4) - 2))
    ap.add_argument("--out", default="/tmp/goblins_truth.json")
    ap.add_argument("--score", help="score the CURRENT build against an existing table")
    ap.add_argument("--label", help="label for --score output")
    a = ap.parse_args()
    if not os.path.exists(BIN):
        print(f"missing {BIN} -- run ./build.sh first", file=sys.stderr)
        return 2
    if a.score:
        score(a)
    else:
        build(a)
    return 0


if __name__ == "__main__":
    sys.exit(main())

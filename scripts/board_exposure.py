#!/usr/bin/env python3
"""Board EXPOSURE, per arm -- a proxy for the one thing a goldfish cannot price.

USER 2026-08-28: "The haste actually has a little advantage that we don't really test. The 'kill
your opponent out of nowhere' advantage that especially is good against decks with sorcery-speed
sweepers."

That value is invisible here BY CONSTRUCTION: the opponent is a goldfish, so there is no sweeper and
exposing a board for a turn costs exactly zero. This does not price the sweeper risk -- nothing in
this harness can. What it CAN do is rank the arms on how much exposure they buy, which is the input
that risk would act on:

  exposure_turns   turns that END with a creature on board, before the winning turn.
                   Every one of these is a window a Wrath-style effect could use.
  peak_exposed     the largest end-of-turn creature count before the winning turn -- how much
                   is standing there to be swept at the worst moment.
  nowhere_kill     fraction of WINS where the board at the end of the PREVIOUS turn held <= 1
                   creature. This is the "out of nowhere" kill: the lethal board appeared on the
                   turn it killed, so a sorcery-speed answer never got a window at it.

Reuses the screen's own manifest, so the apparatus (arm decklists, pooled profile, numbering) is
identical to the measured run -- only the job list and game count are subset.

  python3 scripts/board_exposure.py <screen.manifest.json> --arms cur,ie0_an4 [--games 1500]

NOTE ON THE SCHEMA (verified against a real trace, not assumed -- an earlier analysis of mine
printed a column of zeros by assuming a `power` field that does not exist):
  top level   {cardNumbering, deckId, gameNumber, mulliganSequence, openingHand, result, runId,
               seed, turns}
  result      {"turn": <int>, "winner": "player"|...}     <- NOT "winTurn"
  turns[]     {turn, phase, actions, boardAfter}          <- one entry PER PHASE, not per turn
  battlefield [{card, cardName, tapped, isLand, ...}]     <- no power, no is_creature
Creature-ness is resolved by NAME against cards.json (tokens included, since CreateToken names them
"<P>/<T> <subtype> Token"). End-of-turn count = the LAST phase entry for that turn.
"""
import argparse, collections, json, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


# CreateToken names a creature token "<P>/<T> <first subtype> Token", and MOST such tokens have no
# cards.json entry at all (only ones needing abilities do -- Treasure, the Eldrazi Spawn). Verified
# against a real corpus: "1/1 Goblin Token" and "1/1 Soldier Token" are creatures and are absent
# from the card DB, while "Treasure Token" is NOT a creature and correctly fails this pattern
# because it carries no P/T prefix. A name-set lookup alone silently scores every token as zero.
TOKEN_CREATURE = re.compile(r"^\d+/\d+ .+ Token$")


def creature_names():
    """Names that are creatures -- real cards; tokens are matched by TOKEN_CREATURE instead."""
    db = json.load(open(os.path.join(ROOT, "src/cards/data/cards.json")))
    cards = db["cards"] if isinstance(db, dict) and "cards" in db else db
    out = set()
    for c in cards:
        if "Creature" in (c.get("types") or []):
            out.add(c["name"])
    return out


def is_creature(perm, creatures):
    if perm.get("isLand"):
        return False
    n = perm.get("cardName") or ""
    return n in creatures or bool(TOKEN_CREATURE.match(n))


def analyse(trace_dir, job, creatures):
    """-> dict of exposure stats for one job's traces."""
    exposure, peak, nowhere, wins, games = [], [], 0, 0, 0
    pat = re.compile(re.escape(job) + r"_gi\d+\.json$")
    for fn in sorted(os.listdir(trace_dir)):
        if not pat.match(fn):
            continue
        games += 1
        try:
            g = json.load(open(os.path.join(trace_dir, fn)))
        except Exception:
            continue
        res = g.get("result") or {}
        win_turn = res.get("turn", -1) if res.get("winner") == "player" else -1

        # end-of-turn creature count, per turn. `turns` holds one entry PER PHASE, so the last
        # entry seen for a turn number is that turn's end state.
        per_turn = {}
        for e in g.get("turns", []):
            b = e.get("boardAfter")
            t = e.get("turn")
            if not b or t is None:
                continue
            n = sum(1 for p in b.get("battlefield", []) if is_creature(p, creatures))
            per_turn[t] = n

        if win_turn <= 0:
            continue
        wins += 1
        before = [n for t, n in per_turn.items() if t < win_turn]
        exposure.append(sum(1 for n in before if n >= 1))
        peak.append(max(before) if before else 0)
        prev = per_turn.get(win_turn - 1, 0)
        if prev <= 1:
            nowhere += 1

    mean = lambda v: sum(v) / len(v) if v else float("nan")
    return {"games": games, "wins": wins,
            "exposure_turns": mean(exposure), "peak_exposed": mean(peak),
            "nowhere_kill": (nowhere / wins) if wins else float("nan")}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("manifest")
    ap.add_argument("--arms", required=True, help="comma-separated job names")
    ap.add_argument("--games", type=int, default=1500)
    ap.add_argument("--outdir", default="logs/deckcmp/exposure")
    ap.add_argument("--threads", type=int, default=os.cpu_count())
    ap.add_argument("--reuse", action="store_true", help="skip the batch, analyse existing traces")
    a = ap.parse_args()

    arms = [s.strip() for s in a.arms.split(",") if s.strip()]
    src = json.load(open(a.manifest))
    jobs = [j for j in src["jobs"] if j["name"] in arms]
    missing = set(arms) - {j["name"] for j in jobs}
    if missing:
        sys.exit(f"not in manifest: {', '.join(sorted(missing))}\n"
                 f"available: {', '.join(j['name'] for j in src['jobs'])}")

    outdir = os.path.join(ROOT, a.outdir)
    traces = os.path.join(outdir, "traces")
    os.makedirs(traces, exist_ok=True)

    if not a.reuse:
        for j in jobs:
            j["games"] = a.games          # same seed => a prefix of the measured games
        man = os.path.join(outdir, "exposure.manifest.json")
        json.dump({"jobs": jobs}, open(man, "w"), indent=1)
        print(f"[exposure] {len(jobs)} arms x {a.games} games -> {traces}", flush=True)
        rc = subprocess.call([os.path.join(ROOT, "build/Release/mtg"), "--batch", man,
                              "--threads", str(a.threads), "--game-trace-dir", traces],
                             cwd=ROOT)
        if rc != 0:
            sys.exit(f"batch failed rc={rc}")

    creatures = creature_names()
    print("\n  (a goldfish cannot price sweeper risk; this ranks how much exposure each arm buys)\n")
    print(f"  {'arm':14}{'games':>7}{'wins':>7}{'exposure turns':>16}{'peak exposed':>14}{'nowhere kill':>14}")
    rows = []
    for j in jobs:
        s = analyse(traces, j["name"], creatures)
        rows.append((j["name"], s))
        print(f"  {j['name']:14}{s['games']:>7}{s['wins']:>7}{s['exposure_turns']:>16.2f}"
              f"{s['peak_exposed']:>14.2f}{s['nowhere_kill']*100:>13.1f}%")
    if len(rows) == 2:
        (n1, s1), (n2, s2) = rows
        print(f"\n  {n2} vs {n1}:  exposure {s2['exposure_turns']-s1['exposure_turns']:+.2f} turns,"
              f"  nowhere-kill {(s2['nowhere_kill']-s1['nowhere_kill'])*100:+.1f} pp")
        print("  A LOWER exposure / HIGHER nowhere-kill arm is worth more than its win turn says,")
        print("  because the risk it avoids is the risk this harness scores at zero.")


if __name__ == "__main__":
    main()

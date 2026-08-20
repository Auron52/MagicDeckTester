#!/usr/bin/env python3
"""Emit the CASE LOGS for one tournament comparison: up to 500 games where the swap changed the
result and the contested card was actually cast.

    python3 scripts/tourney_cases.py --a tf3lib0 --b tf0lib3 --factor tf --life 20 \
        --games 10000 [--cap 500] [--threads 32]

User spec (2026-08-19): "beyond that we should also emit logs for up to 500 of the better/worse
games. AI should analyze these looking for problems. It should be the case that the card was cast
in these cases. (cast or played if it is a land) Then, a human can take a look at some of them as
well or run those games in the play viewer."

Why the traces are re-generated rather than kept: the measurement run writes one ~120-byte
[cards] line per game instead of a ~20 KB JSON trace, which is the difference between 145 MB and
24 GB for 1.2M games. The few hundred games a report actually shows are replayed here with
--game-log-dir, and the replay is VERIFIED to reproduce the measured win turn -- a case log of a
different game than the one that was counted would be worse than no case log at all.

Selection: divergent games only (the arms reached different win turns), split evenly between the
two directions, spread round-robin across the contexts so no single context can fill the quota,
and ordered by |delta| inside a context so the largest swings come first.
"""
import argparse, collections, json, os, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import tourney_report as tr  # noqa: E402
import tourney_run as trun   # noqa: E402

CASES = os.path.join(ROOT, "logs", "tourney", "cases")

# Which apparatus a run used decides the profile the REPLAY must use. Replaying against the wrong
# one does not error -- it silently produces different games, which the win-turn verification below
# then catches (it caught exactly this: 320 of 1,000 games came back with a different win turn).
APPARATUS = {
    "A": dict(err="logs/tourney/run_alias/tourney.err",
              profile="logs/tourney/alias/alias.profile.json",
              value="logs/tourney/alias/alias.value.json",
              armdir="logs/tourney/arms", base="tf3lib0_a4o0_scale"),
    "B": dict(err="logs/tourney/run_B/tourney.err",
              profile="logs/tourney/aliasB/aliasB.profile.json",
              value="logs/tourney/aliasB/aliasB.value.json",
              armdir="logs/tourney/arms2", base="tf3lib0_a4s2"),
}

CONTEXTS = {
    "tf":   ([(x, y) for x in tr.AO for y in tr.SLOT], lambda l, c: f"{l}_{c[0]}_{c[1]}"),
    "ao":   ([(x, y) for x in tr.TF for y in ("scale", "draught", "entrance")],
             lambda l, c: f"{c[0]}_{l}_{c[1]}"),
    "slot": ([(x, y) for x in tr.TF for y in tr.AO], lambda l, c: f"{c[0]}_{c[1]}_{l}"),
}


def select_pairs(data, pairs, life, games, cap):
    """pairs: [(armA, armB), ...] -- an explicit comparison, so a STRATUM can be logged (e.g. Anger
    vs Oracle only in the Scale context, which is where the two cards actually disagree)."""
    bits = tr.slot_bits(pairs[0][0], pairs[0][1])
    per = collections.defaultdict(lambda: ([], []))
    for a0, b0 in pairs:
        a, b = f"{a0}@L{life}", f"{b0}@L{life}"
        if a not in data or b not in data:
            continue
        wa, sa, ca = data[a]; wb, sb, cb = data[b]
        for gi in range(games):
            if wa[gi] == wb[gi] or not ((ca[gi] | cb[gi]) & bits):
                continue
            rec = {"ctx": a0, "armA": a, "armB": b, "gi": gi,
                   "wtA": wa[gi], "wtB": wb[gi], "delta": wb[gi] - wa[gi]}
            per[a0][0 if wb[gi] < wa[gi] else 1].append(rec)
    for c in per:
        for k in (0, 1):
            per[c][k].sort(key=lambda r: (-abs(r["delta"]), r["gi"]))
    picked, half = [], cap // 2
    keys = [p[0] for p in pairs if p[0] in per]
    for k in (0, 1):
        take, i = [], 0
        while len(take) < half:
            added = False
            for c in keys:
                lst = per[c][k]
                if i < len(lst) and len(take) < half:
                    take.append(lst[i]); added = True
            if not added:
                break
            i += 1
        picked += take
    return picked, bits


def select(data, factor, la, lb, life, games, cap, maxt):
    ctxs, name_of = CONTEXTS[factor]
    ctxs = [c for c in ctxs
            if f"{name_of(la, c)}@L{life}" in data and f"{name_of(lb, c)}@L{life}" in data]
    if not ctxs:
        sys.exit(f"no context has both {la} and {lb} in this run")
    bits = tr.slot_bits(name_of(la, ctxs[0]), name_of(lb, ctxs[0]))
    per_ctx = collections.defaultdict(lambda: ([], []))   # ctx -> (better, worse)
    for c in ctxs:
        a, b = f"{name_of(la, c)}@L{life}", f"{name_of(lb, c)}@L{life}"
        wa, sa, ca = data[a]
        wb, sb, cb = data[b]
        for gi in range(games):
            if wa[gi] == wb[gi]:
                continue
            # The card must have been CAST (or, for a land, played) in the arm that plays it --
            # the user's rule. Either side satisfies it: the swapped numbers are the same slots.
            if not ((ca[gi] | cb[gi]) & bits):
                continue
            rec = {"ctx": "_".join(c), "armA": a, "armB": b, "gi": gi,
                   "wtA": wa[gi], "wtB": wb[gi], "delta": wb[gi] - wa[gi]}
            per_ctx[c][0 if wb[gi] < wa[gi] else 1].append(rec)
    for c in per_ctx:
        for k in (0, 1):
            per_ctx[c][k].sort(key=lambda r: (-abs(r["delta"]), r["gi"]))
    picked, half = [], cap // 2
    for k in (0, 1):          # 0 = B better, 1 = A better -- filled to equal quotas
        take, i = [], 0
        while len(take) < half:
            added = False
            for c in ctxs:
                lst = per_ctx.get(c, ([], []))[k]
                if i < len(lst) and len(take) < half:
                    take.append(lst[i])
                    added = True
            if not added:
                break
            i += 1
        picked += take
    return picked, bits


def replay(picked, life, games, seed, threads, tag, profile, value, armdir, base_deck):
    """Re-run exactly the selected games with traces on. -> (trace dir, index path)."""
    out = os.path.join(CASES, tag)
    traces = os.path.join(out, "traces")
    os.makedirs(traces, exist_ok=True)
    numbering = {a: os.path.join(armdir, a + ".numbering.json")
                 for a in {r[side].split("@")[0] for r in picked for side in ("armA", "armB")}}
    jobs, want = [], {}
    for r in picked:
        for side in ("armA", "armB"):
            arm = r[side].split("@")[0]
            name = f"{r[side]}#g{r['gi']}"
            jobs.append({
                "name": name,
                "deck": os.path.relpath(os.path.join(armdir, arm + ".txt"), ROOT),
                "deck_numbering": os.path.relpath(numbering[arm], ROOT),
                # games=1 with seed = base+gi and game_index = gi reproduces the batch game
                # exactly: BatchRunner shuffles on job.seed + local index and names the trace by
                # job.game_index + local index, and here the local index is 0.
                "games": 1, "seed": seed + r["gi"], "game_index": r["gi"],
                "max_turns": 8, "starting_life": life,
                "profile": os.path.relpath(profile, ROOT),
                "value_profile": os.path.relpath(value, ROOT),
                "value_model": False, "ladder_value_leaf": True,
            })
            want[name] = r["wtA"] if side == "armA" else r["wtB"]
    man = os.path.join(out, "replay.manifest.json")
    json.dump({"jobs": jobs}, open(man, "w"), indent=1)
    err = os.path.join(out, "replay.err")
    env = {**os.environ, "MTG_DUMP_WINS": "1",
           "MTG_PROVIDER_DECK": os.path.abspath(base_deck)}
    with open(err, "w") as e, open(os.path.join(out, "replay.out"), "w") as o:
        rc = subprocess.call([os.path.join(ROOT, "build/Release/mtg"), "--batch", man,
                              "--threads", str(threads), "--game-trace-dir", traces],
                             stdout=o, stderr=e, cwd=ROOT, env=env)
    if rc != 0:
        raise SystemExit(f"replay failed rc={rc}; see {err}")
    got = {}
    for line in open(err):
        if line.startswith("[win] "):
            _, j, g, w = line.split()
            wt = int(w[3:])
            got[j[4:]] = 9 if (wt < 0 or wt > 8) else wt   # same scoring the report uses
    bad = [k for k, v in want.items() if got.get(k) != v]
    if bad:
        raise SystemExit(
            f"REFUSING these case logs: {len(bad)} of {len(want)} replayed games reached a "
            f"DIFFERENT win turn than the measurement recorded (e.g. {bad[:3]}). The traces would "
            "describe games that were never counted. Check that the profile, value profile, "
            "starting life and max_turns match the measurement manifest.")
    return traces, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--apparatus", choices=["A", "B"], default="A")
    ap.add_argument("--err", default="")
    ap.add_argument("--a", default=""); ap.add_argument("--b", default="")
    ap.add_argument("--factor", default="", choices=[""] + list(CONTEXTS))
    ap.add_argument("--pairs", default="",
                    help="explicit 'armA:armB,armA2:armB2' -- lets a STRATUM be logged, e.g. Anger "
                         "vs Oracle only in the Scale context where the two actually disagree")
    ap.add_argument("--tag", default="")
    ap.add_argument("--life", type=int, required=True)
    ap.add_argument("--games", type=int, required=True)
    ap.add_argument("--seed", type=int, default=1200000)
    ap.add_argument("--cap", type=int, default=500)
    ap.add_argument("--threads", type=int, default=os.cpu_count() or 32)
    ap.add_argument("--max-turns", type=int, default=8)
    a = ap.parse_args()
    A = APPARATUS[a.apparatus]
    err = a.err or os.path.join(ROOT, A["err"])
    profile = os.path.join(ROOT, A["profile"]); value = os.path.join(ROOT, A["value"])
    armdir = os.path.join(ROOT, A["armdir"]); base = os.path.join(armdir, A["base"] + ".txt")
    data = tr.load(err, a.games, a.max_turns)
    if a.pairs:
        pairs = [tuple(x.split(":")) for x in a.pairs.split(",") if x.strip()]
        tag = a.tag or f"pairs_{pairs[0][0]}_vs_{pairs[0][1]}_L{a.life}"
        picked, bits = select_pairs(data, pairs, a.life, a.games, a.cap)
        A_lbl, B_lbl = pairs[0]
    else:
        if not (a.factor and a.a and a.b):
            sys.exit("give either --pairs or --factor/--a/--b")
        tag = a.tag or f"{a.factor}_{a.a}_vs_{a.b}_L{a.life}"
        picked, bits = select(data, a.factor, a.a, a.b, a.life, a.games, a.cap, a.max_turns)
        A_lbl, B_lbl = a.a, a.b
    if not picked:
        sys.exit(f"{tag}: no divergent games where the contested card was cast")
    nb = sum(1 for r in picked if r["delta"] < 0)
    print(f"  {tag}: {len(picked)} cases ({nb} where the SECOND arm wins sooner, "
          f"{len(picked) - nb} where the first does)")
    traces, out = replay(picked, a.life, a.games, a.seed, a.threads, tag, profile, value,
                         armdir, base)
    na = tr.numbering(picked[0]["armA"].split("@")[0])
    nbm = tr.numbering(picked[0]["armB"].split("@")[0])
    idx = {"comparison": {"A": A_lbl, "B": B_lbl, "life": a.life, "apparatus": a.apparatus},
           "swapped_numbers": sorted(k for k in range(1, 61) if bits >> k & 1),
           "swapped_names": {"A": sorted({na[k] for k in na if na.get(k) != nbm.get(k)}),
                             "B": sorted({nbm[k] for k in nbm if na.get(k) != nbm.get(k)})},
           "viewer": "open tools/replay/index.html and drag a trace onto the page",
           "traces": os.path.relpath(traces, ROOT),
           "cases": [{**r,
                      "traceA": os.path.relpath(os.path.join(traces, f"{r['armA']}#g{r['gi']}_gi{r['gi']}.json"), ROOT),
                      "traceB": os.path.relpath(os.path.join(traces, f"{r['armB']}#g{r['gi']}_gi{r['gi']}.json"), ROOT),
                      "repro": f"--seed {a.seed + r['gi']} --game-index {r['gi']} --games 1"}
                     for r in picked]}
    p = os.path.join(out, "cases.json")
    json.dump(idx, open(p, "w"), indent=1)
    print(f"  -> {os.path.relpath(p, ROOT)}  ({len(picked) * 2} traces)")


if __name__ == "__main__":
    main()

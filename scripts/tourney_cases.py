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

CONTEXTS = {
    "tf":   ([(x, y) for x in tr.AO for y in tr.SLOT], lambda l, c: f"{l}_{c[0]}_{c[1]}"),
    "ao":   ([(x, y) for x in tr.TF for y in ("scale", "draught", "entrance")],
             lambda l, c: f"{c[0]}_{l}_{c[1]}"),
    "slot": ([(x, y) for x in tr.TF for y in tr.AO], lambda l, c: f"{c[0]}_{c[1]}_{l}"),
}


def select(data, factor, la, lb, life, games, cap, maxt):
    ctxs, name_of = CONTEXTS[factor]
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


def replay(picked, life, games, seed, threads, tag):
    """Re-run exactly the selected games with traces on. -> (trace dir, index path)."""
    out = os.path.join(CASES, tag)
    traces = os.path.join(out, "traces")
    os.makedirs(traces, exist_ok=True)
    numbering = {a: os.path.join(trun.ARMS, a + ".numbering.json") for a in trun.arm_names()}
    jobs, want = [], {}
    for r in picked:
        for side in ("armA", "armB"):
            arm = r[side].split("@")[0]
            name = f"{r[side]}#g{r['gi']}"
            jobs.append({
                "name": name,
                "deck": os.path.relpath(trun.arm_path(arm), ROOT),
                "deck_numbering": os.path.relpath(numbering[arm], ROOT),
                # games=1 with seed = base+gi and game_index = gi reproduces the batch game
                # exactly: BatchRunner shuffles on job.seed + local index and names the trace by
                # job.game_index + local index, and here the local index is 0.
                "games": 1, "seed": seed + r["gi"], "game_index": r["gi"],
                "max_turns": 8, "starting_life": life,
                "profile": os.path.relpath(trun.PROFILE, ROOT),
                "value_profile": os.path.relpath(trun.VALUE, ROOT),
                "value_model": False, "ladder_value_leaf": True,
            })
            want[name] = r["wtA"] if side == "armA" else r["wtB"]
    man = os.path.join(out, "replay.manifest.json")
    json.dump({"jobs": jobs}, open(man, "w"), indent=1)
    err = os.path.join(out, "replay.err")
    env = {**os.environ, "MTG_DUMP_WINS": "1",
           "MTG_PROVIDER_DECK": os.path.abspath(trun.arm_path(trun.BASE_ARM))}
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
    ap.add_argument("--err", default="logs/tourney/run/tourney.err")
    ap.add_argument("--a", required=True)
    ap.add_argument("--b", required=True)
    ap.add_argument("--factor", required=True, choices=list(CONTEXTS))
    ap.add_argument("--life", type=int, required=True)
    ap.add_argument("--games", type=int, required=True)
    ap.add_argument("--seed", type=int, default=1200000)
    ap.add_argument("--cap", type=int, default=500)
    ap.add_argument("--threads", type=int, default=os.cpu_count() or 32)
    ap.add_argument("--max-turns", type=int, default=8)
    a = ap.parse_args()
    tag = f"{a.factor}_{a.a}_vs_{a.b}_L{a.life}"
    data = tr.load(a.err, a.games, a.max_turns)
    picked, bits = select(data, a.factor, a.a, a.b, a.life, a.games, a.cap, a.max_turns)
    nb = sum(1 for r in picked if r["delta"] < 0)
    print(f"  {tag}: {len(picked)} cases ({nb} where {a.b} wins sooner, {len(picked) - nb} where "
          f"{a.a} does), across {len({r['ctx'] for r in picked})} contexts")
    traces, out = replay(picked, a.life, a.games, a.seed, a.threads, tag)
    nm = numbering_names(a, picked)
    idx = {"comparison": {"factor": a.factor, "A": a.a, "B": a.b, "life": a.life},
           "swapped_numbers": sorted(k for k in range(1, 61) if bits >> k & 1),
           "swapped_names": nm,
           "traces": os.path.relpath(traces, ROOT),
           "cases": [{**r,
                      "traceA": os.path.relpath(os.path.join(traces, f"{r['armA']}#g{r['gi']}_gi{r['gi']}.json"), ROOT),
                      "traceB": os.path.relpath(os.path.join(traces, f"{r['armB']}#g{r['gi']}_gi{r['gi']}.json"), ROOT),
                      "repro": f"--seed {a.seed + r['gi']} --game-index {r['gi']} --games 1"}
                     for r in picked]}
    p = os.path.join(out, "cases.json")
    json.dump(idx, open(p, "w"), indent=1)
    print(f"  -> {os.path.relpath(p, ROOT)}  ({len(picked) * 2} traces in "
          f"{os.path.relpath(traces, ROOT)})")


def numbering_names(a, picked):
    if not picked:
        return {}
    na = tr.numbering(picked[0]["armA"].split("@")[0])
    nb = tr.numbering(picked[0]["armB"].split("@")[0])
    return {"A": sorted({na[k] for k in na if na.get(k) != nb.get(k)}),
            "B": sorted({nb[k] for k in nb if na.get(k) != nb.get(k)})}


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Derive a deck's mulligan-GENERATION setting (value_play.mull_gen_*) by measuring it.

Runs at the END of a value-leaf run, where the deck's play settings and value_trust_depth are known.
Answers "which (depth, budget) should this deck label its keep table at?" by scoring the SAME random
openers under each candidate and comparing them to the deck's own shipped play policy.

WHY MEASURE INSTEAD OF DEFAULTING (user, 2026-08-15): "We don't always want to take d3 b3 just
because it is the default. We want to figure out the best option for the deck." The direction is
genuinely deck-shaped -- on fivecolour, LOWER depth is 2.4x MORE expensive
(mullgen-depth-cost-vs-quality.md) -- so a default is a guess, and a wrong guess costs hours-to-days
of generation.

THE TWO RULES, both measured (mullgen-setting-is-a-trust-question.md):

  * TRUSTED at the shipped play depth -> emit NO override, i.e. generate at play settings. Not merely
    acceptable: for such a deck the play settings are frequently the CHEAPEST arm available (slivers
    1,496 units/rollout vs 2,431 for d3 b3) AND perfect by construction, because reaching the value
    leaf terminates the line instead of playing the game out. There is no trade-off to arbitrate.
  * OTHERWISE -> pick the CHEAPEST (depth, budget) pair that clears a rank-fidelity floor. Speed is
    the binding constraint for generation ("within reason"), because a profile that is too expensive
    to generate does not exist at all -- but a labeller that reorders hands produces a different
    policy, so the floor is real and not decorative.

WHY (depth, budget) PAIRS AND NOT A DEPTH LADDER. Reaching the value leaf needs depth >= trust AND
enough budget to COMPLETE that depth; the escalation ladder commits the deepest COMPLETED pass, so a
starved budget pays for an abandoned deeper pass and commits the shallower line anyway. Cost is
therefore NOT monotonic in depth: on slivers d3 b20 costs 11,551 units while d5 b20 costs 1,496 --
going deeper at the same budget is 7.7x cheaper.

WHY RANDOM OPENERS AND NOT BUCKET COMPS. The comp scorer needs a committed exhaustive sidecar for its
bucket map, but that sidecar is an OUTPUT of the generation this script configures -- on a new deck it
does not exist exactly when the derivation wants it. MTG_SCORE_HANDS needs no buckets, no discovery
and no prior profile.
"""
import argparse
import json
import math
import os
import pathlib
import re
import subprocess
import sys

BIN_CANDIDATES = ["build/Release/mtg-analyze", "build/Profile/mtg-analyze"]


def spearman(a, b):
    n = len(a)
    if n < 3:
        return float("nan")

    def ranks(v):
        order = sorted(range(n), key=lambda i: v[i])
        r = [0.0] * n
        i = 0
        while i < n:
            j = i
            while j + 1 < n and v[order[j + 1]] == v[order[i]]:
                j += 1
            avg = (i + j) / 2.0 + 1.0
            for k in range(i, j + 1):
                r[order[k]] = avg
            i = j + 1
        return r

    ra, rb = ranks(a), ranks(b)
    ma, mb = sum(ra) / n, sum(rb) / n
    num = sum((x - ma) * (y - mb) for x, y in zip(ra, rb))
    da = math.sqrt(sum((x - ma) ** 2 for x in ra))
    db = math.sqrt(sum((y - mb) ** 2 for y in rb))
    return num / (da * db) if da > 0 and db > 0 else float("nan")


def score(binary, deck, cards, depth, budget, hands, R, seed):
    """-> (labels, units_per_rollout). Labels are the play/draw mean per opener."""
    env = dict(os.environ,
               MTG_SCORE_COMPS="1", MTG_SCORE_HANDS=str(hands), MTG_SCORE_R=str(R),
               MTG_EQUIV_DEPTH=str(depth), MTG_SCORE_BUDGET_MS=str(budget),
               MTG_SCORE_HAND_SEED=str(seed))
    p = subprocess.run([binary, str(deck), "--cards-json", cards],
                       capture_output=True, text=True, env=env)
    if p.returncode != 0:
        raise SystemExit("scorer failed (d%s b%s):\n%s" % (depth, budget, p.stderr[-2000:]))
    labels = []
    for line in p.stdout.splitlines():
        parts = line.split("\t")
        if len(parts) < 3:
            continue
        try:
            dm = float(parts[1].split()[0])
            pm = float(parts[2].split()[0])
        except (ValueError, IndexError):
            continue
        labels.append((dm + pm) / 2.0)
    m = re.search(r"units_per_rollout=([0-9.]+)", p.stderr)
    return labels, (float(m.group(1)) if m else 0.0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("deck", help="path to the decklist (.txt/.cod)")
    ap.add_argument("--cards-json", default="src/cards/data/cards.json")
    ap.add_argument("--hands", type=int, default=48)
    ap.add_argument("-R", type=int, default=24)
    ap.add_argument("--seed", type=int, default=424242)
    ap.add_argument("--floor-rho", type=float, default=0.99,
                    help="minimum rank fidelity vs the deck's shipped play policy")
    ap.add_argument("--write", action="store_true",
                    help="write the pick into the deck's .value.json value_play")
    args = ap.parse_args()

    deck = pathlib.Path(args.deck)
    binary = next((b for b in BIN_CANDIDATES if os.path.exists(b)), None)
    if binary is None:
        raise SystemExit("no mtg-analyze binary; run ./build.sh first")

    vpath = deck.parent / (deck.stem + ".value.json")
    if not vpath.exists():
        raise SystemExit("no value sidecar at %s -- run the value-leaf first (this step is its "
                         "LAST phase, and it needs the play settings the matrix derived)" % vpath)
    vjson = json.load(open(vpath))
    vp = vjson.get("value_play") or {}
    trust = vjson.get("value_trust_depth")
    play_d, play_b = int(vp.get("target_depth") or 0), int(vp.get("budget_ms") or 0)
    if not play_d or not play_b:
        raise SystemExit("value_play.target_depth/budget_ms unset -- nothing to reference against")

    print("deck=%s  play=d%d b%d  value_trust_depth=%s" % (deck.stem, play_d, play_b, trust))

    # ------------------------------------------------------------------ the trusted short-circuit
    if trust is not None and trust <= play_d:
        print("\nTRUSTED at the play depth (V%d <= d%d) -> generate at PLAY SETTINGS; emit no "
              "override." % (trust, play_d))
        print("  Reaching the leaf terminates the rollout instead of playing it out, so play settings "
              "are\n  typically both the cheapest arm AND exact. Nothing to trade off.")
        if args.write:
            dropped = [k for k in ("mull_gen_depth", "mull_gen_budget_ms") if k in vp]
            for k in dropped:
                vp.pop(k)
            if dropped:
                json.dump(vjson, open(vpath, "w"), indent=1)
                print("  dropped %s from %s" % (",".join(dropped), vpath.name))
        return

    # ------------------------------------------------------------------ the untrusted measurement
    cands = [(1, 3), (2, 3), (3, 3), (3, 20)]
    if trust is not None:
        cands.append((trust, 3))
        cands.append((trust, play_b))
    cands.append((play_d, play_b))
    seen, ordered = set(), []
    for c in cands:
        if c not in seen:
            seen.add(c)
            ordered.append(c)

    print("\nscoring %d openers at R=%d against the play reference d%d b%d ...\n"
          % (args.hands, args.R, play_d, play_b))
    ref, ref_cost = score(binary, deck, args.cards_json, play_d, play_b, args.hands, args.R, args.seed)

    print("%-10s %8s %12s %10s" % ("arm", "rho", "units/roll", "cost_x"))
    rows = []
    for (d, b) in ordered:
        if (d, b) == (play_d, play_b):
            lab, cost = ref, ref_cost
            rho = 1.0
        else:
            lab, cost = score(binary, deck, args.cards_json, d, b, args.hands, args.R, args.seed)
            if len(lab) != len(ref):
                print("  d%-2d b%-3d SKIPPED (label count mismatch)" % (d, b))
                continue
            rho = spearman(lab, ref)
        rows.append(dict(d=d, b=b, rho=rho, cost=cost))
        print("d%-2d b%-4d %8.4f %12.0f %10.3f"
              % (d, b, rho, cost, cost / ref_cost if ref_cost else float("nan")))

    ok = [r for r in rows if r["rho"] >= args.floor_rho]
    if not ok:
        best = max(rows, key=lambda r: r["rho"])
        print("\nNO candidate clears the rho floor %.3f. Best is d%d b%d at %.4f."
              % (args.floor_rho, best["d"], best["b"], best["rho"]))
        print("Falling back to PLAY SETTINGS (no override) -- generating under a labeller that "
              "measurably\nreorders hands is not a saving, it is a different policy.")
        return

    pick = min(ok, key=lambda r: r["cost"])
    print("\nPICK: d%d b%d  (rho %.4f >= floor %.3f, cheapest clearing it; %.2fx the play cost)"
          % (pick["d"], pick["b"], pick["rho"], args.floor_rho,
             pick["cost"] / ref_cost if ref_cost else float("nan")))
    if (pick["d"], pick["b"]) == (play_d, play_b):
        print("  == play settings; emit no override.")
    elif args.write:
        vp["mull_gen_depth"] = pick["d"]
        vp["mull_gen_budget_ms"] = pick["b"]
        vjson["value_play"] = vp
        json.dump(vjson, open(vpath, "w"), indent=1)
        print("  written to %s" % vpath.name)
    else:
        print("  (re-run with --write to record it)")


if __name__ == "__main__":
    main()

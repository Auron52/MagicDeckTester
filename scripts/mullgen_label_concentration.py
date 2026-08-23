#!/usr/bin/env python3
"""Ask whether a cheap mulligan-GEN labeller's error is CONCENTRATED on a deck's nonlinear hands.

`derive_mullgen_setting.py` picks the cheapest (depth, budget) whose Spearman rank fidelity against the
shipped play policy clears a floor. That floor is an AVERAGE over random openers, and an average is
exactly the statistic that cannot see a large error confined to a small subclass -- the same dilution
that made Colossus Hammer and O-Naginata measure 0.01 apart over 400 probes while their conditional
effects differ by 0.7 turns (decks/KittyEquipment/KittyEquipment.buckets.json).

The subclass that matters for a gen labeller is the deck's MULTI-STEP line: hands whose value only
exists if the search is deep enough to find the sequence. On KittyEquipment that is Colossus Hammer
(equip {8}) plus a cheat (Puresteel metalcraft / Balan). A depth-1 labeller cannot see that line, so it
would price those hands as if the Hammer were a brick -- and it would still score ~0.998 overall,
because such hands are a small slice of random openers.

Usage:
  scripts/mullgen_label_concentration.py decks/KittyEquipment/KittyEquipment.cod \
      --arms 1:3,2:3,3:3 --ref 5:20 --hands 120 -R 24 \
      --group "Colossus Hammer" --group "Colossus Hammer+Puresteel Paladin,Balan, Wandering Knight"

A --group is `A+B,C` = hands containing A AND (B or C). Each group is reported against its complement.
"""
import argparse
import os
import pathlib
import re
import subprocess
import sys

BIN_CANDIDATES = ["build/Release/mtg-analyze", "build/Profile/mtg-analyze"]


def score(binary, deck, cards, depth, budget, hands, R, seed):
    """-> ([(hand_cards, label)], units_per_rollout)."""
    env = dict(os.environ,
               MTG_SCORE_COMPS="1", MTG_SCORE_HANDS=str(hands), MTG_SCORE_R=str(R),
               MTG_EQUIV_DEPTH=str(depth), MTG_SCORE_BUDGET_MS=str(budget),
               MTG_SCORE_HAND_SEED=str(seed))
    p = subprocess.run([binary, str(deck), "--cards-json", cards],
                       capture_output=True, text=True, env=env)
    if p.returncode != 0:
        raise SystemExit("scorer failed (d%s b%s):\n%s" % (depth, budget, p.stderr[-2000:]))
    rows = []
    for line in p.stdout.splitlines():
        parts = line.split("\t")
        if len(parts) < 3 or not parts[0].startswith("H"):
            continue
        try:
            dm = float(parts[1].split()[0])
            pm = float(parts[2].split()[0])
        except (ValueError, IndexError):
            continue
        cards_in_hand = parts[0].split(":", 1)[1].split("|")
        rows.append((cards_in_hand, (dm + pm) / 2.0))
    m = re.search(r"units_per_rollout=([0-9.]+)", p.stderr)
    return rows, (float(m.group(1)) if m else 0.0)


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
    da = sum((x - ma) ** 2 for x in ra) ** 0.5
    db = sum((y - mb) ** 2 for y in rb) ** 0.5
    return num / (da * db) if da > 0 and db > 0 else float("nan")


def parse_group(spec):
    """'A+B,C' -> [{A}, {B, C}]: every clause must be satisfied by at least one of its cards."""
    return [set(c.strip() for c in clause.split(",")) for clause in spec.split("+")]


def matches(hand, clauses):
    hs = set(hand)
    return all(hs & clause for clause in clauses)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("deck")
    ap.add_argument("--cards-json", default="src/cards/data/cards.json")
    ap.add_argument("--arms", default="1:3,2:3,3:3", help="comma list of depth:budget candidates")
    ap.add_argument("--ref", default="5:20", help="reference (shipped play) depth:budget")
    ap.add_argument("--hands", type=int, default=120)
    ap.add_argument("-R", type=int, default=24)
    ap.add_argument("--seed", type=int, default=424242)
    ap.add_argument("--group", action="append", default=[],
                    help="'A+B,C' = hands with A and (B or C); repeatable")
    args = ap.parse_args()

    binary = next((b for b in BIN_CANDIDATES if os.path.exists(b)), None)
    if binary is None:
        raise SystemExit("no mtg-analyze binary; run ./build.sh first")
    deck = pathlib.Path(args.deck)

    rd, rb = (int(x) for x in args.ref.split(":"))
    print("reference d%d b%d, %d hands x R=%d, seed %d" % (rd, rb, args.hands, args.R, args.seed))
    ref_rows, ref_cost = score(binary, deck, args.cards_json, rd, rb, args.hands, args.R, args.seed)
    hands = [r[0] for r in ref_rows]
    ref = [r[1] for r in ref_rows]
    print("  %d hands scored, %.0f units/rollout\n" % (len(ref), ref_cost))

    groups = [(g, parse_group(g)) for g in args.group]
    for gname, clauses in groups:
        n = sum(1 for h in hands if matches(h, clauses))
        print("group %-52s %3d/%d hands (%.1f%%)" % (gname, n, len(hands), 100.0 * n / max(1, len(hands))))
    print()

    hdr = "%-9s %8s %10s %9s" % ("arm", "rho", "units/rol", "cost_x")
    for gname, _ in groups:
        hdr += " | %-28s" % (gname[:28])
    print(hdr)

    for arm in args.arms.split(","):
        d, b = (int(x) for x in arm.split(":"))
        rows, cost = score(binary, deck, args.cards_json, d, b, args.hands, args.R, args.seed)
        if len(rows) != len(ref):
            print("d%-2d b%-3d SKIPPED (label count mismatch)" % (d, b))
            continue
        lab = [r[1] for r in rows]
        line = "d%-2d b%-5d %8.4f %10.0f %9.3f" % (d, b, spearman(lab, ref), cost,
                                                   cost / ref_cost if ref_cost else float("nan"))
        for gname, clauses in groups:
            idx = [i for i, h in enumerate(hands) if matches(h, clauses)]
            oth = [i for i in range(len(hands)) if i not in set(idx)]
            # Signed bias inside the group vs outside: a labeller that cannot SEE a line prices those
            # hands WORSE (higher win turn), so a positive in-group bias against a ~0 out-group bias is
            # the fingerprint of a blind spot rather than of uniform noise.
            def bias(ii):
                return sum(lab[i] - ref[i] for i in ii) / len(ii) if ii else float("nan")
            line += " | in %+.4f out %+.4f" % (bias(idx), bias(oth))
        print(line)

    print("\nRead: 'in' is the mean signed label error on the group's hands, 'out' on every other hand.")
    print("A cheap arm is SAFE for this deck when in ~= out. A materially larger 'in' means the arm is")
    print("blind to the line those hands are built around, whichever way the overall rho reads.")


if __name__ == "__main__":
    sys.exit(main())

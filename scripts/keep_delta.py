#!/usr/bin/env python3
"""MEASURE d -- how far a keep table's cell values move when refit to an edited deck's library.

`keep_margin.py` bounds the screening apparatus's rollout-half bias as a function of d, for free and
at any K. d is the one input that has to be measured, and it is what makes every bound conditional
until it is known.

Measuring it does NOT need a table. d is a per-cell quantity, so a SAMPLE of cells settles its typical
magnitude, and `MTG_SCORE_COMPS` scores chosen compositions at chosen R. The cost is
(cells x R x 2 decks x 2 pd) rollouts -- INDEPENDENT of the grid, so it is the same price on Hinata2's
431k-cell table as on burn's 10.9k one. That is the whole point: a full regeneration is unaffordable on
this deck set (six of the nine shipped tables were themselves generated below R=40), and this is not.

Two properties make the estimate much sharper than the same comparison drawn from generated tables:

  PAIRED.  `RunScoreCompsMode`'s rollout seed is `777e6 + golden*(r+1) + 100003*pd` -- a function of
           the rollout index and pd ONLY, not of the deck or the composition. Base and arm therefore
           run the SAME seed sequence, so their sampling noise is correlated and largely cancels in
           the per-cell difference. (The libraries differ, so the cancellation is partial.)
  NOISE-SUBTRACTED.  The scorer reports a per-cell se, so d^2 = mean(dV^2) - mean(se_base^2 + se_arm^2)
           removes what remains.

Cells are sampled by the ARM's hand mass, which is also what makes the sample well-defined: a cell
holding a card the arm cut has zero probability under the arm and never arises, and a cell the arm can
draw but the base table never enumerated is the separate `fallback_rate` case the reweight gate
already measures.

    python3 scripts/keep_delta.py decks/burn --arm "Skullcrack=0,Lightning Bolt=8" --cells 200 --R 200

Writes <out>/delta.json. Two `mtg` invocations (base, arm), each internally threaded across all cores
over the whole cell list -- one pool per deck, not a loop over cells.
"""
import argparse
import json
import math
import os
import random
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from keep_margin import HAND, deck_counts, deck_paths, read_json  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MTG = os.path.join(ROOT, "build/Release/mtg-analyze")   # the comp scorer lives in the ANALYZER binary


def parse_arm(s):
    out = {}
    if not s.strip():
        return out
    for part in s.split(","):
        name, _, n = part.rpartition("=")
        out[name.strip()] = int(n)
    return out


def table_of(deck_dir):
    _, _, prof = deck_paths(deck_dir)
    t = read_json(prof)
    return t if "buckets" in t else t["exhaustive_keep"]


def sample_cells(ek, base_counts, arm_counts, deck_size, n, sizes, seed):
    """n cells drawn WITHOUT replacement, with probability proportional to the arm's hand mass.

    Restricted to cells the base table holds (the shared apparatus has no value for the others) and
    that the arm can actually draw. Sub-size cells are sampled too: they carry the bottoming argmin,
    which is half the decision surface and the half with far more near-ties."""
    rng = random.Random(seed)
    per_size = {}
    for H in sizes:
        pool = []
        # Size-7 cells come straight from the table; smaller ones are subcompositions of them, which
        # is exactly how the engine reads them (KeepVal minimises over the hand's subcomps).
        if H == HAND:
            for e in ek["entries"]:
                comp = tuple(e["comp"])
                if any(comp[b] > arm_counts[b] for b in range(len(comp))):
                    continue                      # zero probability under the arm
                w = 1
                for b, x in enumerate(comp):
                    w *= math.comb(arm_counts[b], x)
                if w:
                    pool.append((w / math.comb(deck_size, HAND), comp))
        else:
            seen = set()
            for e in ek["entries"]:
                comp = tuple(e["comp"])
                if any(comp[b] > arm_counts[b] for b in range(len(comp))):
                    continue
                # one representative subcomp per size-7 cell: drop from the largest bucket first
                c = list(comp)
                for _ in range(HAND - H):
                    b = max(range(len(c)), key=lambda i: c[i])
                    c[b] -= 1
                sub = tuple(c)
                if sub in seen:
                    continue
                seen.add(sub)
                w = 1
                for b, x in enumerate(comp):
                    w *= math.comb(arm_counts[b], x)
                if w:
                    pool.append((w / math.comb(deck_size, HAND), sub))
        if not pool:
            continue
        k = min(n, len(pool))
        # Weighted sample without replacement (Efraimidis-Spirakis keys).
        keyed = sorted(((rng.random() ** (1.0 / w) if w > 0 else 0.0), c) for w, c in pool)
        per_size[H] = [c for _, c in keyed[-k:]]
    return per_size


def write_arm_deck(deck_dir, arm_counts_by_name, out):
    """A decklist beside a PLAIN copy of the base's keep sidecar.

    `RunScoreCompsMode` builds the sidecar path as `<stem>.keepmodel.exhaustive.profile.json` and does
    NOT resolve `.gz` -- but every deck ships the sidecar gzipped, so pointing the scorer at a deck
    folder loads an empty profile, leaves K=0, fills no hand at all, and reports every composition as
    unwon (max_turns+1) with se 0 instead of failing. Both the base and the arm therefore get a
    scratch directory with the sidecar materialised uncompressed."""
    lst, _, prof = deck_paths(deck_dir)
    # Always emit a .txt: the scorer keys the sidecar off the decklist's STEM, which is preserved, and
    # a .cod's XML is not a line format. Half the deck set (Hinata2, Knights, ...) ships .cod.
    stem = os.path.basename(lst).rsplit(".", 1)[0] + ".txt"
    os.makedirs(out, exist_ok=True)
    per = {}
    order = []
    if lst.endswith(".cod"):
        import xml.etree.ElementTree as ET
        for zone in ET.parse(lst).getroot().iter("zone"):
            if (zone.get("name") or "main") != "main":
                continue
            for card in zone.iter("card"):
                nm = card.get("name")
                if nm not in per:
                    order.append(nm)
                per[nm] = per.get(nm, 0) + int(card.get("number", 1))
    else:
        for line in open(lst):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if line.lower().startswith("sideboard"):
                break
            n, name = line.split(" ", 1)
            per[name.strip()] = int(n)
            order.append(name.strip())
    for name, n in arm_counts_by_name.items():
        if name not in per:
            order.append(name)
        per[name] = n
    with open(os.path.join(out, stem), "w") as f:
        for name in order:
            if per.get(name):
                f.write(f"{per[name]} {name}\n")
    # Link the shipped sidecar (and its .bincache) rather than materialising plain JSON. Now that the
    # scorer resolves `.gz`, this keeps the fast load path: re-parsing Hinata2's 431k-cell table from
    # expanded JSON costs MINUTES per invocation and dwarfs the rollouts the run is trying to measure.
    base = os.path.join(out, stem.rsplit(".", 1)[0] + ".keepmodel.exhaustive.profile.json")
    for suffix in ("", ".bincache"):
        src, dst = os.path.abspath(prof) + suffix, base + (".gz" if prof.endswith(".gz") else "") + suffix
        if os.path.lexists(dst):
            os.unlink(dst)
        if os.path.exists(src):
            os.symlink(src, dst)
    return os.path.join(out, stem), per


def score(deck_path, cell_file, R, depth, out_path, budget_ms=None):
    env = dict(os.environ, MTG_SCORE_COMPS="1", MTG_SCORE_FILE=cell_file,
               MTG_SCORE_R=str(R), MTG_EQUIV_DEPTH=str(depth))
    if budget_ms is not None:
        env["MTG_SCORE_BUDGET_MS"] = str(budget_ms)
    with open(out_path, "w") as f:      # deck path is positional (argv[1]); cards.json resolves off CWD
        subprocess.run([MTG, deck_path], env=env, stdout=f, cwd=ROOT, check=True)
    got = {}
    for line in open(out_path):
        if "\t" not in line:
            continue
        key, d, p = line.rstrip("\n").split("\t")
        dm, dse = (float(x) for x in d.split())
        pm, pse = (float(x) for x in p.split())
        got[key] = ((dm, dse), (pm, pse))
    return got


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("deck")
    ap.add_argument("--arm", default="", help='e.g. "Skullcrack=0,Lightning Bolt=8"')
    # Same deck, two GENERATION budgets. The keep table's cell values are labels produced by the
    # rollout search, so a cheaper budget shifts them -- but only the part of the shift that is NOT
    # common to all cells can flip a keep decision, because Dopt is a weighted average of the same
    # KeepVals and moves with them. That is why a "notable" quality loss can be nearly free here, and
    # it is exactly what mean-vs-dispersion separates.
    ap.add_argument("--budget-ab", default="", metavar="B1,B2",
                    help="compare the SAME deck at two search budgets instead of two decklists")
    ap.add_argument("--cells", type=int, default=200, help="cells per hand size")
    ap.add_argument("--R", type=int, default=200)
    ap.add_argument("--depth", type=int, default=5)
    ap.add_argument("--sizes", default="7,6,5")
    ap.add_argument("--seed", type=int, default=20260812)
    ap.add_argument("--out", default=None)
    a = ap.parse_args()

    out = a.out or os.path.join(ROOT, "logs/keep_margin", os.path.basename(os.path.abspath(a.deck)) + "_delta")
    os.makedirs(out, exist_ok=True)
    ek = table_of(a.deck)
    lst, _, _ = deck_paths(a.deck)
    base_counts, deck_size = deck_counts(lst, ek["buckets"])
    arm_by_name = parse_arm(a.arm)
    # Both count vectors come from the actual decklists rather than from patching per bucket, so a
    # card that shares a bucket with another cannot be double-counted.
    budgets = [int(x) for x in a.budget_ab.split(",")] if a.budget_ab else [None, None]
    if a.budget_ab and a.arm:
        raise SystemExit("--budget-ab compares one decklist at two budgets; drop --arm")
    if not (a.budget_ab or a.arm):
        raise SystemExit("give --arm (two decklists) or --budget-ab (one decklist, two budgets)")
    base_list, _ = write_arm_deck(a.deck, {}, os.path.join(out, "base"))
    arm_list = (base_list if a.budget_ab
                else write_arm_deck(a.deck, arm_by_name, os.path.join(out, "arm"))[0])
    arm_counts, arm_size = deck_counts(arm_list, ek["buckets"])
    if arm_size != deck_size:
        print(f"note: arm mainboard {arm_size} vs base {deck_size} -- deck-size change, weights differ")

    sizes = [int(x) for x in a.sizes.split(",")]
    cells = sample_cells(ek, base_counts, arm_counts, arm_size, a.cells, sizes, a.seed)
    cell_file = os.path.join(out, "cells.txt")
    keys = []
    with open(cell_file, "w") as f:
        for H, cs in cells.items():
            for c in cs:
                line = f"{H}:{','.join(str(x) for x in c)}"
                f.write(line + "\n")
                keys.append((H, line))
    total = len(keys)
    print(f"deck {os.path.basename(os.path.abspath(a.deck))}  K={len(ek['buckets'])}  "
          f"{'budgets=' + a.budget_ab if a.budget_ab else 'arm=' + a.arm}"
          f"\n{total} cells x R={a.R} x 2 arms x 2 pd = "
          f"{total * a.R * 4:,} rollouts (grid-size independent)")

    base_out = score(base_list, cell_file, a.R, a.depth, os.path.join(out, "base.tsv"), budgets[0])
    arm_out = score(arm_list, cell_file, a.R, a.depth, os.path.join(out, "arm.tsv"), budgets[1])
    for lbl, got in (("base", base_out), ("arm", arm_out)):
        if not got:
            raise SystemExit(f"{lbl}: the scorer produced no rows")
        # The K=0 failure above is silent and looks like data, so refuse it explicitly: an all-unwon,
        # zero-variance result means no hand was ever filled, not that the deck cannot win.
        if all(v[0][1] == 0.0 and v[1][1] == 0.0 for v in got.values()):
            raise SystemExit(f"{lbl}: every cell came back with se=0 -- the scorer filled no hand "
                             f"(is the keep sidecar readable beside the decklist?)")

    rows, agg = [], {}
    for H, key in keys:
        b, m = base_out.get(key), arm_out.get(key)
        if not (b and m):
            continue
        for pd, lbl in ((1, "play"), (0, "draw")):
            dv = m[pd][0] - b[pd][0]
            nz = b[pd][1] ** 2 + m[pd][1] ** 2
            rows.append(dict(H=H, cell=key, pd=lbl, dv=dv, noise=nz))
            agg.setdefault((H, lbl), []).append((dv, nz))

    # d is BRACKETED rather than point-estimated, because the two available estimators err in known,
    # opposite directions:
    #   rms dV        UPPER bound -- it is d plus whatever sampling noise the pairing did not cancel
    #   subtracted    LOWER bound -- it removes the noise two INDEPENDENT runs would have, but the
    #                 shared seed sequence already cancelled part of that, so it over-subtracts
    #                 (which is why it goes negative at small R, where noise dominates)
    # The bias bound only needs the upper end, so a loose bracket is still usable; the lower end says
    # how much of the upper end is real.
    print(f"\n  {'size':>5s} {'pd':>5s} {'n':>5s} {'mean dV':>10s} {'d <= (rms)':>11s} "
          f"{'indep noise':>12s} {'d >= (subtr)':>13s}")
    summary = {}
    for (H, lbl), vs in sorted(agg.items()):
        n = len(vs)
        mean = sum(d for d, _ in vs) / n
        ms = sum(d * d for d, _ in vs) / n
        nz = sum(z for _, z in vs) / n
        d2 = ms - nz
        d = math.sqrt(d2) if d2 > 0 else -math.sqrt(-d2)
        se_d2 = math.sqrt(2.0 / n) * ms          # sampling error of the mean-square
        summary[f"H{H}_{lbl}"] = dict(n=n, mean=mean, rms=math.sqrt(ms), noise=math.sqrt(nz), d=d,
                                      se_d2=se_d2)
        print(f"  {H:5d} {lbl:>5s} {n:5d} {mean:+10.4f} {math.sqrt(ms):11.4f} "
              f"{math.sqrt(nz):12.4f} {d:+13.4f}")
    json.dump(dict(deck=a.deck, arm=a.arm, R=a.R, cells=a.cells, rows=rows, summary=summary),
              open(os.path.join(out, "delta.json"), "w"), indent=1)
    print(f"\nwrote {os.path.join(out, 'delta.json')}")


if __name__ == "__main__":
    main()

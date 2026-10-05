#!/usr/bin/env python3
"""Does phase A's EXPENSIVE 10% of games actually buy any model quality?  ANSWER: YES. DO NOT CAP.

STATUS: ASKED AND ANSWERED 2026-10-05. The idea this script was written to test -- cap phase A's
per-game label cost and take a ~12x cheaper generation -- is **REFUTED**, and separately the USER
has declined it outright (*"I don't want to cap phase A"*). This file is kept as the apparatus and
the record, NOT as a live proposal. Do not re-propose capping without new evidence that beats the
table below.

    RMSE on the IDENTICAL held-out games (Fungus candidate-b, 13,047 rows, phase B's recipe):
      arm                all-test    expensive        cheap
      A_all                0.5214       0.6226       0.5093
      B_cheap_only         0.5273       0.6809       0.5080
      B - A               +0.0059      +0.0583      -0.0013

Dropping the expensive games is free on easy positions (-0.0013, noise) and costs **9.4% relative
RMSE on the hard ones**. That is precisely what `EmitEvalRows` already predicted in source before
anyone measured it: a truncated label "would teach the model that a position we could not AFFORD to
solve is a position we cannot WIN from, and that lie is worst exactly on the hardest positions,
which are the ones worth learning." The expensive rows are expensive BECAUSE they are hard, and the
hard positions are where the model's error lives.

One caveat that bounds the result rather than overturning it: arm B drops **every** row of an
expensive game, whereas a real `MTG_VALUE_LABEL_BUDGET_MS` cap would drop only the POSITIONS that
blew the budget (an expensive game's five positions are not all expensive). So +0.0583 is a
PESSIMISTIC proxy for a cap, and the exact figure would need one bounded phase A re-run, not an
offline experiment. It does not change the direction: the hard rows carry signal.

WHY IT EXISTED. Phase A of the value leaf is heavy-tailed to an absurd degree. Measured on Fungus
candidate-b (2,500 games, 4.60 h, 24 cores):

    the 267 games over 30 s =  10.7% of games  =  91.7% of ALL phase-A work units
    ...and they contribute      1,508 rows     =  11.6% of the 13,047 training rows

So 91.7% of the compute buys 11.6% of the data -- a ~70x gap in cost-per-row. If the model is as
good without those rows, phase A can cap per-game label cost and get roughly an order of magnitude
cheaper with NO engine change, which dominates any amount of better scheduling.

IT IS NOT OBVIOUS, AND THE CODE ITSELF ARGUES THE OTHER WAY. `EmitEvalRows` drops a position whose
label search truncated, and says why: a budget-cut search reports max_turns+1, which is
byte-identical to "unwinnable", so keeping it "would teach the model that a position we could not
AFFORD to solve is a position we cannot WIN from, and that lie is worst exactly on the hardest
positions, which are the ones worth learning." Expensive games are expensive BECAUSE their positions
need deep search, i.e. they are plausibly the informative ones. Hence: measure, do not assume.

THE TRAP THIS SCRIPT EXISTS TO AVOID. The trainer holds out every 4th decision internally and
prints `heldout_RMSE`. You cannot answer this question by deleting the expensive rows and comparing
that printed number, because deleting them changes the held-out set too -- arm B would be scored on
an EASIER test set and would look good for free. So the test set is fixed here, built once, and it
deliberately CONTAINS expensive-game rows; both arms are scored on exactly those rows.

Splitting is by GAME (seed), never by row: a game's positions are correlated, so holding out
individual rows would leak a game's own trajectory into its test rows.

The headline is NOT the overall RMSE -- it is the `expensive-only` subgroup, because that is where
dropping the rows must show up first if it hurts at all.

Usage:
    python3 test/tools/leaf_ab/label_cost_vs_value.py \
        --rows logs/vlq_<key>/rows/all.rows \
        --batch-log logs/vlq_<key>/rows.batch.log \
        --out-dir logs/label_cost_ab
"""
import argparse
import collections
import json
import math
import os
import re
import subprocess
import sys

SCALE = 1000.0
# Phase B's recipe, verbatim (scripts/valueleaf.sh phase_train). Keep these in lockstep: the point
# is to compare two TRAINING SETS, so any other difference between the arms invalidates the answer.
RECIPE = ["--regression", "--trees", "120", "--depth", "4", "--lr", "0.15", "--min-leaf", "20"]
TRAINER = "scripts/attic/train_eval_gbdt.py"


def read_rows(path):
    """-> (header_line, [(seed, turn, raw_line, label, {feat: value})])"""
    header, out, names = None, [], None
    for line in open(path):
        line = line.rstrip("\n")
        if not line.strip():
            continue
        if line.startswith("#"):
            header = line
            names = line[1:].split()[1:-2]
            continue
        t = line.split()
        if len(t) < 4:
            continue
        feats = dict(zip(names, (float(v) for v in t[1:-2])))
        out.append((int(t[-2]), int(t[-1]), line, float(t[0]), feats))
    if header is None:
        sys.exit("%s has no '# label ...' header -- cannot name features" % path)
    return header, out


def expensive_seeds(batch_log):
    """Games the batch reported as SLOW (>30 s by default) -> {seed: units}."""
    txt = open(batch_log).read()
    return {int(m.group(2)): int(m.group(1)) for m in re.finditer(
        r"SLOW-GAME \d+ms.*?units=(\d+)\s+repro: --seed (\d+) --game-index (\d+)", txt)}


def job_units(batch_log):
    return sum(int(m) for m in re.findall(r"^\S+: played=\d+ .*units=(\d+)",
                                          open(batch_log).read(), re.M))


def write_rows(path, header, rows):
    with open(path, "w") as f:
        f.write(header + "\n")
        for r in rows:
            f.write(r[2] + "\n")


def predict(model, feats):
    v = model["intercept"] / SCALE
    for tree in model["trees"]:
        i = 0
        while len(tree[i]) > 1:
            name, thr, l, r = tree[i]
            i = l if feats[name] <= thr else r
        v += tree[i][0] / SCALE
    return v


def rmse(model, rows):
    if not rows:
        return float("nan")
    return math.sqrt(sum((predict(model, r[4]) - r[3]) ** 2 for r in rows) / len(rows))


def train(rows_path, out_path, log_path):
    with open(log_path, "w") as lg:
        subprocess.run([sys.executable, TRAINER, "--rows", rows_path, "--out", out_path] + RECIPE,
                       stdout=lg, stderr=lg, check=True)
    return json.load(open(out_path))["eval_model"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rows", required=True, help="phase A's banked all.rows")
    ap.add_argument("--batch-log", required=True, help="phase A's rows.batch.log (for SLOW-GAME lines)")
    ap.add_argument("--out-dir", default="logs/label_cost_ab")
    ap.add_argument("--test-every", type=int, default=4, help="hold out every Nth GAME (default 4 = 25%%)")
    a = ap.parse_args()
    os.makedirs(a.out_dir, exist_ok=True)

    header, rows = read_rows(a.rows)
    exp = expensive_seeds(a.batch_log)
    total_units = job_units(a.batch_log)
    seeds = sorted({r[0] for r in rows})
    test_seeds = set(seeds[::a.test_every])

    test = [r for r in rows if r[0] in test_seeds]
    train_all = [r for r in rows if r[0] not in test_seeds]
    train_cheap = [r for r in train_all if r[0] not in exp]
    test_exp = [r for r in test if r[0] in exp]
    test_cheap = [r for r in test if r[0] not in exp]

    exp_units = sum(exp.values())
    print("=== phase A, as measured ===")
    print("  games=%d  rows=%d  expensive games (SLOW-GAME)=%d" % (len(seeds), len(rows), len(exp)))
    print("  expensive games: %.1f%% of games, %.1f%% of rows, %.1f%% of ALL work units"
          % (100.0 * len(exp) / len(seeds),
             100.0 * sum(1 for r in rows if r[0] in exp) / len(rows),
             100.0 * exp_units / total_units if total_units else float("nan")))
    print()
    print("=== split (by GAME, never by row) ===")
    print("  TEST  games=%d rows=%d  (expensive rows in test=%d -- the subgroup that matters)"
          % (len(test_seeds), len(test), len(test_exp)))
    print("  arm A train rows=%d  (all training games)" % len(train_all))
    print("  arm B train rows=%d  (expensive training games DROPPED: -%d rows, -%.1f%%)"
          % (len(train_cheap), len(train_all) - len(train_cheap),
             100.0 * (len(train_all) - len(train_cheap)) / len(train_all)))
    print()

    write_rows(os.path.join(a.out_dir, "test.rows"), header, test)
    write_rows(os.path.join(a.out_dir, "trainA.rows"), header, train_all)
    write_rows(os.path.join(a.out_dir, "trainB.rows"), header, train_cheap)

    out = {}
    for arm, src in (("A_all", "trainA.rows"), ("B_cheap_only", "trainB.rows")):
        print("training %s ..." % arm)
        m = train(os.path.join(a.out_dir, src),
                  os.path.join(a.out_dir, "%s.model.json" % arm),
                  os.path.join(a.out_dir, "%s.train.log" % arm))
        out[arm] = (rmse(m, test), rmse(m, test_exp), rmse(m, test_cheap))

    print()
    print("=== RMSE on the IDENTICAL held-out games (turns; lower is better) ===")
    print("  %-14s %12s %12s %12s" % ("arm", "all-test", "expensive", "cheap"))
    for arm in ("A_all", "B_cheap_only"):
        r = out[arm]
        print("  %-14s %12.4f %12.4f %12.4f" % (arm, r[0], r[1], r[2]))
    d = [out["B_cheap_only"][i] - out["A_all"][i] for i in range(3)]
    print("  %-14s %+12.4f %+12.4f %+12.4f" % ("B - A", d[0], d[1], d[2]))
    print()
    print("  POSITIVE = dropping the expensive games made the model WORSE.")
    print("  Read the `expensive` column first: it is where the loss must appear if there is one.")
    print("  If it is ~0, phase A can cap per-game label cost for ~%.1fx less compute."
          % (total_units / (total_units - exp_units) if total_units > exp_units else float("nan")))


if __name__ == "__main__":
    main()

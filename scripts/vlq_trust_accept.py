#!/usr/bin/env python3
"""ACCEPTANCE TEST for a value-leaf trust depth: play it on vs off, and let the games decide.

User, 2026-08-15: *"Thinking about it realistically how we should be handling trust is by playing
with it A/B on vs off in additional games and verifying that the results are good. So the tolerance
here would just gate an acceptance test."*

WHY THE MATRIX CANNOT SETTLE THIS. `value_trust_depth` says the hybrid may KEEP a value-leaf line
without escalating it -- the one lever on which a weak leaf costs QUALITY rather than time
(docs/design/value-leaf-quality-floor.md). The depth matrix measures the two arms SEPARATELY and
UNBOUNDED; trust is a claim about what happens when leaf lines are kept inside real, BUDGETED play,
where the saved escalation is spent widening the search instead. Those are different experiments, so
a tolerance read off the table can only ever nominate a candidate. This runs the experiment.

THE TEST IS NON-INFERIORITY, NOT AN IMPROVEMENT TEST. Trust is a COST lever whose upside is the
escalation it skips; the thing that must be established is that skipping does not cost quality. So
the rule is an upper bound: accept iff the one-sided 95% bound on (ON - OFF) sits at or below --tol.
Requiring ON to measure BETTER would reject a lever that is exactly neutral and much cheaper, which
is the outcome we most expect and most want.

Failing SAFE means NOT accepting: no trust => every unverified line stays eligible to escalate.

  usage: vlq_trust_accept.py <batch.log> <staged-model.json> [--tol 0.002] [--on trustON]
                             [--off trustOFF] [--apply]

Without --apply it prints the verdict and writes nothing. With --apply an ACCEPTED candidate is
promoted into `value_trust_depth` in the staged model -- which is still not adoption: the staged
model only goes live when a human installs it.
"""
import argparse
import collections
import json
import math
import re
import sys

# Same shape as vlq_ab_report.py's: an optional "<deck>-" prefix, the arm, then "_s<seed>".
LINE = re.compile(r"(?:[\w]+-)?(\w+?)_s(\d+): played=(\d+) avg=([\d.]+) digest=(\w+)(?: ms=(\d+))?")


def read_arms(path):
    arms = collections.defaultdict(dict)
    for ln in open(path):
        m = LINE.match(ln.strip())
        if m:
            arms[m.group(1)][int(m.group(2))] = (int(m.group(3)), float(m.group(4)),
                                                 m.group(5), int(m.group(6) or 0))
    return arms


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("model")
    ap.add_argument("--tol", type=float, default=0.002,
                    help="non-inferiority margin in LP turns: accept iff the one-sided 95%% upper "
                         "bound on (ON - OFF) is at or below this")
    ap.add_argument("--on", default="trustON")
    ap.add_argument("--off", default="trustOFF")
    ap.add_argument("--apply", action="store_true",
                    help="on ACCEPT, promote the candidate into value_trust_depth in the staged model")
    a = ap.parse_args(argv)

    try:
        model = json.load(open(a.model), object_pairs_hook=collections.OrderedDict)
    except (OSError, ValueError) as e:
        print("  trust: SKIP (cannot read %s: %s)" % (a.model, e))
        return 0
    cand = model.get("value_trust_depth_candidate")
    if cand is None:
        print("  trust: no candidate in %s -- the matrix proposed no trust depth, nothing to test"
              % a.model)
        return 0

    arms = read_arms(a.log)
    if a.on not in arms or a.off not in arms:
        print("  trust: SKIP (arms %s/%s not both in the log; found %s)"
              % (a.on, a.off, sorted(arms)))
        return 0
    seeds = sorted(set(arms[a.on]) & set(arms[a.off]))
    if len(seeds) < 2:
        print("  trust: SKIP (only %d paired seed(s))" % len(seeds))
        return 0

    # SEED-TILING INVARIANT (A/B rule 7): per-game identity is base_seed + game_index, so bases
    # spaced closer than games-per-job make the arms REPLAY games -- which reports enormous
    # significance off a handful of distinct hands. Checked here rather than assumed, because this
    # verdict changes what the deck ships.
    ids = set()
    for s in seeds:
        ids.update(range(s, s + arms[a.off][s][0]))
    n_games = sum(arms[a.off][s][0] for s in seeds)
    tiled = len(ids) == n_games

    diffs = [arms[a.on][s][1] - arms[a.off][s][1] for s in seeds]   # + = ON is WORSE
    mean = sum(diffs) / len(diffs)
    sd = (math.sqrt(sum((x - mean) ** 2 for x in diffs) / (len(diffs) - 1))
          if len(diffs) > 1 else 0.0)
    se = sd / math.sqrt(len(diffs)) if sd else 0.0
    upper = mean + 1.645 * se
    cost_on = sum(arms[a.on][s][3] for s in seeds) / 1000.0
    cost_off = sum(arms[a.off][s][3] for s in seeds) / 1000.0
    identical = sum(1 for s in seeds if arms[a.on][s][2] == arms[a.off][s][2])

    accept = tiled and upper <= a.tol
    print("  trust candidate d%d: ON-OFF %+0.5f turns (se %.5f, one-sided 95%% upper %+0.5f) over "
          "%d seeds x %d games; cost %.0f vs %.0f core-s (%.2fx); %d/%d seeds byte-identical"
          % (cand, mean, se, upper, len(seeds), n_games // max(len(seeds), 1),
             cost_on, cost_off, (cost_on / cost_off if cost_off else float("nan")),
             identical, len(seeds)))
    if not tiled:
        print("  trust: REJECTED -- !! SEED OVERLAP (%d distinct game ids for %d games): the arms "
              "replayed games, so this comparison is not trustworthy" % (len(ids), n_games))
    elif identical == len(seeds):
        # Not a failure, but it means the lever never engaged -- accepting would be accepting
        # nothing, and it usually points at a config fault rather than a real equivalence.
        print("  trust: ACCEPTED but the arms are BYTE-IDENTICAL on every seed -- the trust lever "
              "never engaged (no leaf line was ever committed at or above d%d). Check the config "
              "before reading this as evidence." % cand)
    elif accept:
        print("  trust: ACCEPTED (bound %+0.5f <= tol %.4f) -- the escalation skipped by trusting "
              "at d%d does not cost quality%s" % (upper, a.tol, cand,
              ", and it is %.2fx cheaper" % (cost_on / cost_off) if cost_off and cost_on < cost_off
              else ""))
    else:
        print("  trust: REJECTED (bound %+0.5f > tol %.4f) -- keeping leaf lines unverified at d%d "
              "is not established as quality-neutral; shipping UNSET (always eligible to escalate)"
              % (upper, a.tol, cand))

    if a.apply and accept:
        model["value_trust_depth"] = cand
        model.setdefault("value_leaf_table", {})
        if isinstance(model["value_leaf_table"], dict):
            model["value_leaf_table"]["trust_acceptance"] = collections.OrderedDict([
                ("candidate", cand), ("delta_on_minus_off", round(mean, 5)),
                ("se", round(se, 5)), ("upper95", round(upper, 5)), ("tol", a.tol),
                ("seeds", len(seeds)), ("games_per_arm", n_games),
                ("cost_ratio_on_over_off", round(cost_on / cost_off, 3) if cost_off else None),
                ("rule", "non-inferiority: accepted iff the one-sided 95% upper bound on "
                         "(trustON - trustOFF) LP is at or below tol, on seeds disjoint from the "
                         "matrix's. Trust is a cost lever; the claim being tested is that skipping "
                         "the escalation does not cost quality."),
            ])
        json.dump(model, open(a.model, "w"))
        print("  trust: promoted d%d into value_trust_depth of %s (STAGED -- not adopted)"
              % (cand, a.model))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

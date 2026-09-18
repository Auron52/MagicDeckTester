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

# Below this depth, trusting a leaf line needs a HIGHER level of proof (user, 2026-09-18).
# At d5 a committed line is usually a PROVEN win, so trusting it risks nothing; at 3-4 far fewer
# lines are proven, so a trusted line is much more likely to rest on the leaf's ESTIMATE. Every
# trust depth adopted in this repo is 4, 5 or 6.
SHALLOW_TRUST_DEPTH = 5

# Trust ON must be meaningfully CHEAPER than OFF for the lever to have engaged at all.
# Shipped decks measure 0.48x (BreachingDragonstorm) and 0.68x (CritterLifegain);
# the dead Angels run measured 1.00x.
ENGAGED_COST_RATIO = 0.98

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
    cost_on = sum(arms[a.on][s][3] for s in seeds) / 1000.0
    cost_off = sum(arms[a.off][s][3] for s in seeds) / 1000.0
    identical = sum(1 for s in seeds if arms[a.on][s][2] == arms[a.off][s][2])

    # ---- RESOLUTION FLOOR (the 3*step/n rule dead_rung has had since the burn incident, which
    # this reporter never inherited -- see value-leaf-ab-never-measures-the-benefit.md). Win turns
    # are INTEGERS, so a sample of n games cannot resolve a mean difference finer than ~3/n. With
    # most seeds identical the between-seed sd collapses and manufactures a tiny se, which then
    # passes a non-inferiority bound on no evidence at all. Never let se read below the floor.
    se_floor = 3.0 / n_games if n_games else 0.0
    se_eff = max(se, se_floor)
    upper = mean + 1.645 * se_eff

    # ---- ENGAGEMENT GATE, measured by COST not by play digest. A non-inferiority test rewards a
    # DEAD experiment: if the lever never fired, ON-OFF is exactly 0 with zero variance and the
    # bound passes trivially. That is how Angels shipped a candidate trust depth of 3 -- the LOWEST
    # in the repo -- off a run whose staged sidecar carried leaf:none, so the model was swapped for
    # a Constant() and could never be consulted (2026-09-18).
    #
    # The ORIGINAL warning here blamed "byte-identical on every seed", and that inference is WRONG:
    # trust is a COST lever, so identical play with a big cost saving is the BEST possible outcome,
    # not a dead one. Checked against the decks already shipping a trust depth -- BreachingDragonstorm
    # 0.48x and CritterLifegain 0.68x are both byte-identical AND legitimately accepted, while Angels
    # was byte-identical at 1.00x. Cost is what separates them; the digest is not.
    cost_ratio = (cost_on / cost_off) if cost_off else float("nan")
    engaged = (cost_ratio <= ENGAGED_COST_RATIO) or (identical < len(seeds))

    # ---- SHALLOW TRUST NEEDS A HIGHER BAR (user, 2026-09-18: "Trust less than 5 is an especially
    # difficult case and requires a higher level of proof", "the reason being that many games are
    # won by depth 5, but not so much at 3 or 4").
    # Mechanism: trusting a line means KEEPING it without escalating to verify. At d5 a committed
    # line is usually a PROVEN win, so trusting risks nothing -- the leaf's estimate is not what the
    # decision rests on. At 3-4 far fewer lines are proven, so a trusted line is much more likely to
    # rest on the leaf's ESTIMATE, which is exactly where a weak leaf costs quality, not just time.
    #
    # The bar is expressed as a HALVED tolerance, which -- through the 3/n resolution floor above --
    # automatically demands about twice the evidence rather than being an arbitrary constant:
    # accepting needs 1.645*3/n <= tol, i.e. ~2,500 games at the normal tol and ~5,000 games below
    # d5. A shallow trust depth must therefore be shown quality-neutral over roughly twice as many
    # games as a deep one, which is the "higher level of proof" in a form the sample size enforces.
    shallow = cand < SHALLOW_TRUST_DEPTH
    tol_eff = a.tol / 2.0 if shallow else a.tol
    min_games = math.ceil(1.645 * 3.0 / tol_eff) if tol_eff > 0 else 0
    enough_engagement = n_games >= min_games

    accept = tiled and engaged and enough_engagement and upper <= tol_eff
    print("  trust candidate d%d: ON-OFF %+0.5f turns (se %.5f, one-sided 95%% upper %+0.5f) over "
          "%d seeds x %d games; cost %.0f vs %.0f core-s (%.2fx); %d/%d seeds byte-identical; se %.5f -> %.5f after the 3/n floor; tol %.4f%s"
          % (cand, mean, se, upper, len(seeds), n_games // max(len(seeds), 1),
             cost_on, cost_off, (cost_on / cost_off if cost_off else float("nan")),
             identical, len(seeds), se, se_eff, tol_eff,
             ' (HALVED: shallow trust)' if shallow else ''))
    if not tiled:
        print("  trust: REJECTED -- !! SEED OVERLAP (%d distinct game ids for %d games): the arms "
              "replayed games, so this comparison is not trustworthy" % (len(ids), n_games))
    elif not engaged:
        print("  trust: REJECTED -- the lever NEVER ENGAGED: play is byte-identical on every seed "
              "AND cost is %.2fx (>= %.2f), so trusting at d%d changed nothing at all. A "
              "non-inferiority test passes trivially on a dead experiment, and no evidence is not "
              "evidence of safety. Usual cause: a staged sidecar carrying value_play leaf:none, "
              "which swaps the model for a Constant() so it can never be consulted. Shipping UNSET "
              "(always eligible to escalate)." % (cost_ratio, ENGAGED_COST_RATIO, cand))
    elif not enough_engagement:
        print("  trust: REJECTED -- d%d is SHALLOW (< d%d) and %d games cannot resolve the halved "
              "tolerance %.4f (needs >= %d, since the 3/n floor puts the bound at %+0.5f). Trusting "
              "below d%d keeps lines far more often resting on the leaf's ESTIMATE than on a proven "
              "win, so it needs proof over about twice as many games. Shipping UNSET."
              % (cand, SHALLOW_TRUST_DEPTH, n_games, tol_eff, min_games, upper,
                 SHALLOW_TRUST_DEPTH))
    elif accept:
        print("  trust: ACCEPTED (bound %+0.5f <= tol %.4f%s) -- the escalation skipped by trusting "
              "at d%d does not cost quality%s" % (upper, tol_eff,
              " HALVED: shallow" if shallow else "", cand,
              ", and it is %.2fx cheaper" % (cost_on / cost_off) if cost_off and cost_on < cost_off
              else ""))
    else:
        print("  trust: REJECTED (bound %+0.5f > tol %.4f%s) -- keeping leaf lines unverified at "
              "d%d is not established as quality-neutral; shipping UNSET (always eligible to "
              "escalate)" % (upper, tol_eff, " HALVED: shallow" if shallow else "", cand))

    if a.apply and accept:
        model["value_trust_depth"] = cand
        model.setdefault("value_leaf_table", {})
        if isinstance(model["value_leaf_table"], dict):
            model["value_leaf_table"]["trust_acceptance"] = collections.OrderedDict([
                ("candidate", cand), ("delta_on_minus_off", round(mean, 5)),
                ("se", round(se, 5)), ("se_after_floor", round(se_eff, 5)),
                ("upper95", round(upper, 5)), ("tol", a.tol), ("tol_applied", tol_eff),
                ("shallow_trust", shallow), ("cost_ratio_engaged", round(cost_ratio, 3)),
                ("min_games_for_tol", min_games),
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

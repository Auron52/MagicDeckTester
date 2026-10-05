#!/usr/bin/env python3
"""Value leaf vs no leaf, on 4x the seeds phase E used. ONE pooled batch, any deck.

WHY THIS EXISTS. Phase E measures the leaf on 8 paired seeds x 1000 games, and that is not enough to
adopt on. On KittyEquipment it reported -0.00138 turns at t=-1.17; re-measured at 32 seeds the whole
signal was a coin flip (-0.00025, t=-0.48) and on the 24 seeds phase E never touched the sign
FLIPPED (+0.00013). The standing rule that came out of it: never adopt on a phase E delta alone --
re-run at 4x the seeds with bases spaced by games-per-job, and report the held-out subset separately.

This generalises `test/tools/kitty_ab/gen_leaf_ab_manifest.py`, which hard-coded one deck's vroot and
stem. The protocol is identical for every deck, so the deck belongs in argv, not in the source.

SEED SPACING IS LOAD-BEARING. Game identity is base+game_index, so bases must be spaced by at least
games-per-job or jobs REPLAY each other's games -- phase E's own comment records that closer spacing
"once turned 1.3 sigma into a fake -14.4 sigma". Default 1000 games/job with bases 1000 apart, so
each job tiles its own slice exactly once. `--games` and `--spacing` move together unless you pass
both deliberately.

WHAT MAKES THE COMPARISON CLEAN, and it is worth re-checking per deck rather than assuming:
phase E builds the two arms as sibling directories under its queue's `variants/<key>/`, differing in
exactly one real file, `<stem>.value.json` -- `live` carries no `eval_model` (so AttachValueSidecar
attaches nothing that can rank a plan) and `staged` carries the regenerated one. Everything else,
including the deck's profile and its exhaustive keep table, is symlinked to the same target in both
arms, so an adopted keep profile is held CONSTANT across the arms rather than being a second
variable. Verify before trusting a run:

    diff <arm>/live/<stem>.profile.json <arm>/staged/<stem>.profile.json      # must be empty
    python3 - <<'P'  # staged value_play must have NO `enabled` key, or it steers play too
    import json; print(json.load(open('.../staged/<stem>.value.json'))['value_play'])
    P

`value_play.enabled` is the OTHER meaning of enabled (the play policy). An unenabled block is a pure
recommendation and is byte-identical in play, which is what keeps this an A/B of the MODEL. If the
staged file ever ships an enabled block, this manifest measures model+policy together and the delta
cannot be attributed.

The jobs deliberately set NO `depth` and NO `budget_ms`: both arms run off the profile, at the deck's
real play point. Budget churn is therefore not an available explanation for any difference -- the
engine's budget is in VIRTUAL ms (900 work units each, SearchBudget::FromVirtualMs), so an A/B is
deterministic and load-independent even on a shared box.

COST IS THE WEAKER HALF OF THE OUTPUT. Quality here is deterministic, but the core-s ratio is not:
phase E measured Kitty at 0.61x while 16 sweep jobs shared its pool, and the same arms in a clean
pool measured 0.375x. So read the cost ratio from a pool running nothing else, and treat a contended
number as a floor on the speedup, never as the speedup.

THE ARMS MUST INTERLEAVE IN TIME, AND BY DEFAULT THEY DO NOT -- hence the `weight` field below.
BatchRunner's work list is `stable_sort`ed on (sched_weight desc, depth desc, budget_ms desc,
PROFILE PATH asc, job index), and the profile-path key exists for a good reason: a big exhaustive
keep sidecar is ~103-167 MB, so grouping a profile's games contiguously keeps the resident set at
~1 profile instead of thrashing the ProfileCache. But a value-leaf A/B differentiates its arms BY
THE PROFILE DIRECTORY -- it has to, because the engine resolves every sibling sidecar
directory-relative off the profile path -- so that key sorts ALL of `live` before ALL of `staged`
and the two arms run in sequence, hours apart. On a shared box that does not merely make the cost
ratio a floor, it makes it BIASED by whatever else happened to be running during each arm's half,
in an unknown direction. (Observed 2026-10-05: 24 of 64 jobs in, every completed job was `live`.)
It also violates this repo's own standing rule that A/B arms must interleave on a shared box --
the rule that exists because three A/Bs of the same change once gave three answers and one sign flip.

`weight` is the FIRST sort key, so emitting a per-SEED weight re-sorts the list to
(seed, arm) and pairs the arms in time. This costs NOTHING here: the run has exactly len(arms)
distinct profiles, the ProfileCache default cap is 3, so both stay warm for the whole batch and the
grouping the sort was protecting buys nothing. Pass --no-interleave to get the old grouped order
back (useful only if a run has so many distinct profiles that they cannot all stay resident).

Usage:
    python3 test/tools/leaf_ab/gen_leaf_ab_manifest.py \
        --vroot logs/vlq_<key>/variants/<key> --stem <Stem> [--seeds 32] > /tmp/ab.manifest.json
    ./build/Release/mtg --batch /tmp/ab.manifest.json > /tmp/ab.log

Then split the report at the phase-E boundary: the first `--phase-e-seeds` bases (default 8) are the
ones phase E already used, the rest are fresh. Pooling all of them is legitimate -- the leaf is fit
to phase-A rows, not to these games, so none of them influenced it -- but the claim worth making is
that the effect survives on seeds that had no chance to.
"""
import argparse
import json
import os
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--vroot", required=True,
                    help="phase E's variant root: logs/vlq_<key>/variants/<key> (holds live/ and staged/)")
    ap.add_argument("--stem", required=True, help="artifact stem, e.g. Fungus (the PARENT deck's stem for a variant list)")
    ap.add_argument("--seeds", type=int, default=32, help="number of paired seed bases (default 32 = 4x phase E)")
    ap.add_argument("--games", type=int, default=1000, help="games per job (default 1000, phase E's AB_GAMES)")
    ap.add_argument("--base", type=int, default=600000, help="first seed base (default 600000, phase E's first)")
    ap.add_argument("--spacing", type=int, default=0,
                    help="seed base spacing; 0 = --games, which is the minimum that avoids replaying games")
    ap.add_argument("--arms", default="live,staged", help="arm subdirectory names under --vroot")
    ap.add_argument("--no-interleave", action="store_true",
                    help="omit the per-seed `weight`, letting BatchRunner's profile-path sort run each "
                         "arm as one contiguous block (see the docstring -- this biases the cost ratio)")
    a = ap.parse_args()

    spacing = a.spacing or a.games
    if spacing < a.games:
        sys.exit("refusing: spacing %d < games %d would make jobs replay each other's games "
                 "(phase E: closer spacing 'once turned 1.3 sigma into a fake -14.4 sigma')"
                 % (spacing, a.games))

    arms = [s for s in a.arms.split(",") if s]
    jobs = []
    for i in range(a.seeds):
        seed = a.base + i * spacing
        for arm in arms:
            deck = "%s/%s/%s.cod" % (a.vroot, arm, a.stem)
            prof = "%s/%s/%s.profile.json" % (a.vroot, arm, a.stem)
            for p in (deck, prof):
                if not os.path.exists(p):
                    sys.exit("missing %s -- has phase E run for this deck? (it builds variants/ at "
                             "phase E and rm -rf's it on re-entry)" % p)
            job = {"name": "%s.s%d" % (arm, seed), "deck": deck,
                   "profile": prof, "games": a.games, "seed": seed}
            # Descending so sched_weight (sorted DESC, and the first key) orders by seed ascending;
            # every arm of one seed shares a weight, so the profile-path key then pairs them
            # back-to-back instead of splitting the run into one block per arm.
            if not a.no_interleave:
                job["weight"] = a.seeds - i
            jobs.append(job)
    json.dump({"jobs": jobs}, sys.stdout, indent=1)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()

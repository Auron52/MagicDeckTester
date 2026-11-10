# Shadowborn Apostle: integration checkpoint (2026-11-10 UTC)

The current checkout gained six decks after the original Shadowborn work split into two branches. This checkpoint
tracks integration onto `83e0ad041` without copying either branch's older tree over new decks or references.

## Changes brought forward

* `origin/sb-picks-crossings` (`1c9ef7535` through `c838e3183`): the real-upkeep Profane Tutor fetch is searched;
  Crop Rotation consumes its searched land pin; Search for Tomorrow, Emeria, and Abiding Grace choose at the
  resolution crossing; Titan's attack land choice is evaluated at the attack trigger. These are the five commits
  originally queued under the searched-picks item of `shadowborn-handoff-2026-11-06.md`.
* `origin/sb-sacpayer` (`4f2763a48` through `3189fce5d`): Lotus Bloom and Lake of the Dead are on-demand payment
  sources, their direct `SacForMana` plan actions are retired, the idle-crack fold is removed, and a searched early
  Lake payment option preserves lines that need mana across a ripple breakpoint. The old branch's validation is
  recorded in `sac-for-mana-on-demand.md` and `logs/sb-sacpayer/sacpayer-handoff.md`.
* Integration conflicts kept both sides' independent pins: Profane/land/Grace/Emeria and Miracle at turn crossings;
  early Lake payment and Militia Pride/EDF in plan and dominance keys. A stale fold-test declaration was removed.

The old payer branch's 10,600-game A/B, unit/scenario, regression, and reference results are prior-branch evidence.
The combined branch requires its own validation because the searched-pick changes were not in that measurement and
the current checkout has newer decks and source changes. No old ground-truth files were copied during integration.

## Validation on the combined branch

* Release `mtg` build: PASS with `MTG_BUILD_JOBS=4`.
* Scenarios: 445 passed, zero failed/error.
* Full unit suite: 2,672 cases and 3,579,151 assertions passed, zero failed.
* Full smoke and regression: pending at this checkpoint. Do not accept ground truth until the changed games have
  been reviewed under `.claude/skills/regression-testing.md`.

## Value-leaf sidecar

The approved `logs/vlq_shadowborn_apostle/Shadowborn Apostle.value.EMUL.json` is absent from this workspace and
from the prior branches' tracked files. The deck has no live `.value.json`. Recover that exact artifact if available;
otherwise regenerate after the integrated play change is accepted. `valueleaf.sh run` and its E.7/E.7b/E.6 stages
cannot resume from a missing ignored artifact. The requested leaf0/leafless/budget comparison follows sidecar
availability and payer/pick validation.

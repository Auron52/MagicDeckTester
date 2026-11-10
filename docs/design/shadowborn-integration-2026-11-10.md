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
* Full smoke: 172 pass, 11 fail, zero new across 183 cases. The four searched slower games were classified below.
  This integration did not change any of the eight payer cases relative to the accepted payer branch's per-game
  ground truth; it did not change any of the three other failing cases relative to the current-base binary. Smoke
  ground truth is deliberately untouched. Full regression and strict reference replay remain to run after this
  branch is integrated with the newer mainline source.

## Full smoke audit (2026-11-10 UTC)

Run: `THREADS=4 MTG_PGO=0 MTG_BIN=./build/Release/mtg bash test/regression.sh --smoke`. The batch completed
183 cases in 20m34s. Eleven differ from the committed ground truth. The eight Shadowborn/Dragonstorm cases below
have *byte-identical per-game `.wins` files* to `origin/sb-sacpayer:test/gt_logs/<key>.wins`, including every
win turn and play digest. Thus every changed game in these cases has exactly the already reviewed and accepted
payer-branch result; the crossing-pick integration introduces no additional per-game difference in smoke.

| Case | Current GT avg | Integrated avg | Change |
|---|---:|---:|---:|
| Shadowborn d0 | 7.782 | 7.399 | -0.383 |
| Shadowborn d3 | 5.100 | 5.060 | -0.040 |
| Shadowborn d5 | 4.860 | 4.810 | -0.050 |
| Shadowborn 2HG d3 | 5.320 | 5.280 | -0.040 |
| Dragonstorm d0 | 4.979 | 4.962 | -0.017 |
| Dragonstorm d3 | 4.500 | 4.480 | -0.020 |
| Dragonstorm d5 | 4.460 | 4.440 | -0.020 |
| Dragonstorm 2HG d5 | 4.700 | 4.680 | -0.020 |

The remaining failures are Cyclone d5 (digest only), Landfall d0 (7.440 to 7.443), and Landfall d5
(digest only). The exact pre-integration `83e0ad041:src` PGO binary reproduced all three cases, 1,200 games
total; its `.wins` files are byte-identical to the integration run. These are current-base GT drift, not changes
from this branch. The branch's source integration made no per-game difference in these cases.

The per-game audit reports four searched slower games, 14 searched faster, 26 searched same-score play changes;
at d0, 64 slower, 227 faster, 508 same-score play changes. All these per-game results in the payer rows match
the accepted prior branch exactly. `test/classify_turn_later.sh smoke` reran every searched slower game at 4x
and 16x its case budget:

| Searched game | Case result | 4x / 16x | Classification |
|---|---|---|---|
| Dragonstorm d3 gi31 | T4 to T5 | T5 / T5 | Persists at higher *case* budget; direct d8/b0 repro wins T4 (137 units) |
| Shadowborn 2HG d3 gi1 | T4 to T5 | T4 / T4 | Budget churn |
| Shadowborn d3 gi8 | T6 to T7 | T6 / T6 | Budget churn |
| Shadowborn d5 gi54 | T4 to T5 | T4 / T4 | Budget churn |

The prior payer branch's independent held-out audit found every one of 49 searched slower games recovered at
d8/b0, and its ten regression slower games also recovered. This integration's smoke result is exactly the
prior payer result, while the direct d8/b0 check above resolves its one smoke 16x exception. Examples of d0
slower games in the audit include Dragonstorm gi136 T5 to T6 and gi228 T7 to T8; both belong to the identical
accepted payer result. Landfall gi31 T8 to loss is reproduced byte-for-byte by the pre-integration binary.
The loss-penalized net movement in each payer case is favorable, as shown in the table. No GT import was made.

The smoke check is complete, but merge is conditional on rebasing onto the newer mainline source, running the
full regression and strict reference replay there, and reviewing any additional changed games before GT import.

## Value-leaf sidecar

The approved `logs/vlq_shadowborn_apostle/Shadowborn Apostle.value.EMUL.json` is absent from this workspace and
from the prior branches' tracked files. The deck has no live `.value.json`. Recover that exact artifact if available;
otherwise regenerate after the integrated play change is accepted. `valueleaf.sh run` and its E.7/E.7b/E.6 stages
cannot resume from a missing ignored artifact. The requested leaf0/leafless/budget comparison follows sidecar
availability and payer/pick validation.

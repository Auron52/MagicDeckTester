# An unreproduced determinism anomaly in the regression suite (2026-08-25)

> **READ `mirrorwing-overnight-anomaly-2026-08-25.md` FIRST.** That is the same phenomenon, on the
> same deck, on the same day, investigated far more thoroughly by another agent. **This document is
> the SECOND, INDEPENDENT observation of it** — different tier, different seeds, different agent,
> found while adopting an unrelated flag. Two sightings of an unreproducible bug are worth more than
> either alone, so the comparison is in "Two sightings" below; the hypothesis table further down is
> retained because it eliminates several candidates theirs did not test (and vice versa).
>
> **STATUS: OPEN, one observation, NOT reproduced in 19 subsequent full-scale executions.**
> This document exists so the observation is not lost and so the next person does not re-walk the
> eight hypotheses already eliminated below. It does **not** claim a mechanism. If you see a
> suite key move that your change cannot explain, read this first — and capture the evidence listed
> under "What to capture if it recurs" BEFORE re-running anything, because a re-run destroys it.

## The observation

During the `MTG_EQUIP_DRAW_BP` adoption, one full `bash test/regression.sh` run reported four
MirrorwingDragon keys as FAIL. Mirrorwing contains **no Equipment and no `draw_on_equipment_etb`
watcher**, so the flag under test could not reach it.

```
FAIL  mirrorwing_regression_d3_s2002 exp=4.8150/1c123397f384b0f7 got=4.7950/72a528b69fae9739
FAIL  mirrorwing_regression_d3_s3003 exp=4.7450/73352be242102db9 got=4.7300/bc3cd2b0afeb595f
FAIL  mirrorwing_regression_d5_s2002 exp=4.7600/9204d0be3ebdd44d got=4.7600/0ff5c9419c94ced0
FAIL  mirrorwing_regression_d5_s3003 exp=4.7600/3dd1e5cf001ef3bb got=4.7600/3898832ecbfc96c5
```

Four properties of the anomaly, all of which a mechanism has to explain:

1. **Only SEARCHED cells moved.** `mirrorwing_regression_d0_s2002` passed byte-identical. d0 runs no
   lookahead, so whatever moved lives in the search.
2. **Every move was in the "more/better search" direction** — two cells improved, two kept their
   average and moved only their play digest. Nothing got worse.
3. **The same binary, in the same run, produced the intended KittyEquipment numbers exactly** — the
   five kitty keys read identically in this run and in every later one, so the build and the flag
   were what they were supposed to be.
4. **The value is novel.** `4.7950` is not a historical ground-truth value for that key; git history
   holds only `5.1300`, `4.8050` and `4.8150`. So it is not a stale-GT or wrong-baseline artifact.

Two subsequent full runs were **byte-identical to each other across all 75 keys**, and matched
ground truth for every non-kitty deck.

## Two sightings, and where they disagree

| | overnight sighting (`mirrorwing-overnight-anomaly-2026-08-25.md`) | this one |
|---|---|---|
| tier / seeds | overnight, 4004-10010 | regression, 2002/3003 |
| cells affected | **all 12** Mirrorwing cells | **4** Mirrorwing cells |
| direction | all improving (42 better / 0 worse on one d0 cell) | all improving (2 cells better, 2 digest-only) |
| **d0** | **DIVERGED** | **byte-identical (PASSED)** |
| reproduced? | no — 4 later runs, incl. one with the snapshotted binary | no — 19 later full-scale executions |
| trigger context | another agent committing into the same working tree as the batch started | adopting `MTG_EQUIP_DRAW_BP`, tree quiescent |

**The d0 disagreement is the sharpest discriminator available and it should be the next thing
pursued.** The overnight record reasons from d0 diverging that the cause cannot be a timing or
budget effect, since d0 runs no search and no budget. Here d0 was byte-identical while the searched
cells moved — which is the opposite constraint. Either the two sightings have different causes, or
the true cause can express itself at d0 or not depending on something neither investigation has
identified. Any proposed mechanism has to account for BOTH.

Worth noting explicitly: the three use-after-frees fixed in `303c51be` are **not** the explanation
for either sighting. That commit's own analysis rules Mirrorwing out of all three (no Goblin Lackey,
zero look-effect cards, no Varchild's War-Riders), and this sighting post-dates none of it — but the
UAF write-up describes exactly the failure profile both sightings show ("silent, allocator-dependent,
unreproducible... turns wrong only when the allocator reuses the block, which depends on load, thread
count and heap history"). So the *class* of bug is demonstrated to exist in this codebase even though
these particular three instances are excluded. A fourth instance on a path Mirrorwing DOES reach is
the most promising hypothesis neither investigation has yet tested; an ASAN run of the regression
tier (the overnight matrix was already ASAN'd, which is how those three were found) is the cheap
next step.

## Hypotheses eliminated

Each of these was tested, not reasoned about. None reproduced the anomaly.

| # | Hypothesis | Test | Result |
|---|---|---|---|
| 1 | The flag leaked past its param gate | `MTG_EQUIP_DRAW_BP=0` vs default, `--deck=mirrorwing` | Passes byte-identical **either way** — the flag does not move this deck |
| 2 | Thread-count dependence | the flaky cell at `--threads` 1, 2, 4, 8, 16, 24 | Identical digest at all six |
| 3 | Job-mix / cross-job contamination | 20-job mixed queue (mirrorwing + fivecolour + stompy + kitty), 4 reps | Identical every rep |
| 4 | Non-determinism visible within one process | **twin jobs**: duplicate the mirrorwing searched cells under a second name in the same 75-job batch, 2 reps | Twins agree with each other and with GT |
| 5 | Solve-memo clear timing changes the answer | `MTG_SOLVE_MEMO_CAP` = 4096 / 16384 / 65536 / 262144 | Identical digest at all four — the documented "the cap moves COST, never the answer" invariant **holds** |
| 6 | CPU oversubscription (the recorded prior cause, `overnight-determinism-investigation`) | 32 competing busy loops, forcing a 5.5x wall-clock slowdown (21 s -> 118 s) | Identical digest |
| 7 | Per-game work ceiling / cell condemnation abandoning games | suite manifest carries no `abandon_*` fields; no `[batch] relative per-game ceiling armed` line; every run reported full `played` counts | Not armed — cannot be the cause |
| 8 | Stale ground truth / wrong baseline | `git grep` the key across all history | `4.7950` never was a GT value |

Also ruled out by construction: the solve-memo cannot carry state across games, because entries are
keyed to a monotonically increasing `g_decision_epoch` and a hit demands an exact epoch match, so
every entry from an earlier decision is permanently unreachable.

## What to capture if it recurs

The first investigation was hobbled by destroying its own evidence. **A re-run overwrites
`test/logs/<mode>/wins/`, `test/logs/<mode>/*.log`, `test/results/<mode>.env` and the manifest** —
and `--deck=<name>` overwrites them for that deck too, which is how the per-game diff for the
anomalous run was lost here (running `--deck=mirrorwing` to "check" it clobbered the very games
that had changed). Before re-running anything:

```bash
cp -r test/logs/regression /tmp/anomaly_$(date +%s)      # wins/ + per-case .log/.err + manifest.json
cp test/results/regression.env /tmp/anomaly_env
```

Then the per-game diff that identifies WHICH games moved is a file diff, no re-run needed:

```bash
diff <(sort -n test/gt_logs/<key>.wins) <(sort -n /tmp/anomaly_.../wins/<key>.wins)
```

With the moved game indices in hand, reproduce single-game in isolation
(`--seed <base+gi> --games 1 --game-index <gi> --log-dir <dir>`) and check whether the isolated
reproduction gives the GT answer or the anomalous one. That single fact splits the space in half:
if isolation reproduces the anomaly the cause is per-game and deterministic; if it does not, the
cause is in the batch/process context.

## The cheap standing detector, if we want one

Hypothesis 4's twin trick is the useful residue. Duplicating one cheap searched cell under a second
name inside the same batch costs a couple of seconds and turns "did my change do this, or is the
suite flaky?" — the question that cost the most time here — into an immediate in-run answer: twins
that disagree are proof of non-determinism, independent of ground truth. `auras_regression_d5_s2002`
is the cheapest candidate (auras d5 is ~0.009 s/game). Not wired in; it is a matrix change and
therefore a user decision.

## Why this was not treated as a blocker for the adoption it interrupted

The `MTG_EQUIP_DRAW_BP` rebaseline went ahead because the anomaly did not touch what was being
promoted: kitty's five regression keys read identically in all three runs, the accepted run had every
non-kitty key matching GT exactly, and the committed GT diff was verified to be 20 kitty lines plus
the provenance header and nothing else. The anomaly is a suite-trust problem, not a contamination of
that measurement.

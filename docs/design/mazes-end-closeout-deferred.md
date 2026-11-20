# Maze's End: closeout and deferred work (2026-11-20)

The Maze's End analysis is wrapping up at the USER's request, so the box can go to a new deck. Ledger:
`docs/design/analysis-Maze's End.md`. Rulings history: `docs/design/mazes-end-work-queue.md`.

USER, 2026-11-20 ~21:30 UTC (VERBATIM): "Yes, at this point we should only do high priority items. Ideally you
would be wrapped up in a few hours so I can start a new deck here. And yes, I realize we won't be able to do the
mulligan profile yet. I'm trying to use the reset budget efficiently which means I need to prioritize
high-inference tasks like analyzing over high-CPU tasks for the rest of the week."

## State at closeout
- **Batch 2** is being integrated on `wip/maze-batch2` (base origin 1f3c23fab) and lands through the push queue.
  It contains:
  - maze-shared 39bc4a308: the overnight GT for the 13 new mazesend keys
  - DM-2 311e4c1d9
  - RC 7d4c2f5d4
  - P7 973c795bb
  - F6 ac4b294d0

  Its validation is units, smoke, regression, d8 b0 on games slower on the combined tree, then accept,
  ref_bench and verify_deck. The landing commit is recorded in the ledger.
- **Bars met before batch 2:**
  - ref_bench: 0/11 short (search 5.727 vs human 5.818; s8 one turn faster).
  - verify_deck GATE PASS.
  - GT consistent (`check_gt_logs`: 0 stale).

## DEFERRED: the mulligan profile (next CPU-heavy stage for this deck)
`scripts/mullgen.sh run "decks/Maze's End"`. Run it alone on the box, on the frozen commit that includes batch 2,
with the 2-hourly status timer and a PID waiter (CLAUDE.md overnight rules). Order per CLAUDE.md: profile, then
mulligan, then value leaf. The value leaf also needs P7's b0 work, which is in batch 2.

Before launching, re-confirm on the frozen tip:
- ref_bench 0/11 short;
- verify_deck PASS (pass the `.cod` path, not the folder);
- `suite_gate.py --require "decks/Maze's End"`.

## DEFERRED: measured components awaiting a USER decision (not in batch 2)

| Item | Branch / evidence | Measured | Recommendation |
|---|---|---|---|
| P5d "rev" (fetch-and-replay rule) | `wip/maze-p5d` c5c1ce805; `logs/maze/p5d7/` | passes net held-out; d8 b0: 0 persist, 4 UNRESOLVED (unit ceiling) | adopt |
| P5d revq2 | same | passes net; d8 b0: 11 persist (the q2 component) | out |
| P5d q2, cond, c2, q2nb | same | fail the net bar | out |
| P5b land-twin fold, with the RC fix | `wip/maze-rc1-p5b` 3ebba2691; RC2-handoff | std −2 / 2HG −1; the 3 slower games recover at d8 b0 | adopt (sound fold) |
| Gods dredge deck-out widening | `wip/maze-dmg` 2ea57d65f; `logs/maze/DMG2-handoff.md` | held-out net −64 (d3) / −86 (d5) / −79 (2HG d5); smoke 3 Gods keys faster | finish the d8 b0 check on 48 slower games, then adopt |
| Gods dredge rule 3 / 4 / 5 | same | rule-3/5 A/B never ran | USER's call |
| IDG trial sr2 | `wip/idgate-r2` c587ca270; `logs/idgate/IDG4-handoff.md` | net −62; Martyr Lifegain +2, Hydras +2, Merfolk Turns +1 fail the per-deck bar | QvT run first; not adoptable as is |

## DEFERRED: open engineering threads

| Thread | Where | Next step |
|---|---|---|
| F5R pay-refine levers (budget-charged cap vs the fixed node cap of 08b13f602) | `wip/maze-f5r3` 82b384d18; `logs/maze/F5R5-handoff.md` | `chain3.sh` (6-arm screen, 3660 jobs). Layer-5 refusals are genuine colour mismatches (Baldur's Gate floats {W}{W}{W}{W}); RBT_SUBSET is probably not the lever. Check Galadriel's cost under the cap at the executor's null-budget apply. |
| bp-node wave drop in a lookahead host (RC-3) | `docs/design/bp-node-wave-drop-in-lookahead-host.md`; `wip/maze-rc3-diag` (print-only, never land) | build the per-host gate, A/B it (USER decision) |
| Echo-edge fold | `docs/design/echo-edge-fold-probe-pricing.md` (P7-9) | build and measure |
| IDG memory (Merfolk Turns overnight d3 s4004 gi123 >12 GB on the old base) | `logs/idgate/IDG4-handoff.md` | fixed upstream (cc6edaeef, edf5915a8); confirm the r2 binary stays under the cap |
| Memory-aware `build.sh` job default | `build.sh` ~L155 (`MTG_BUILD_JOBS` / nproc) | jobs = min(nproc, (MemAvailable − reserve) / measured per-compile peak), read live; no machine constant in the repo. A 12-job full build peaked at 14.9 GB summed cc1plus RSS (largest 3.4 GB). Heavy TUs in a smaller ninja pool to raise parallelism. |
| b0 backstop re-screen | work-queue USER ruling 2026-11-18 ~22:15 | after every other item |

## Open USER questions carried forward
1. P5d rev: adopt? (recommend yes)
2. P5b fold: adopt? (recommend yes)
3. Build and A/B the bp-node per-host gate?
4. Manabond q2: prune "yes" when it would discard the held land tutor and no Maze's End is anywhere? (currently it only orders "no" first)
5. Conditional fetch ranking: pursue cond, or drop it? (c2 is out)
6. Burgeoning in 2HG: no measurable effect.
7. Gods dredge rule 3, 4 or 5?
8. Fixed pay-refine node cap, or F5R's budget-charged cap?
9. IDG adoption, after its quality-vs-time run.
10. Baldur's Gate conditional hold: adopted as a clean win; the USER may overrule.

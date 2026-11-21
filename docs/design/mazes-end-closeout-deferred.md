# Maze's End: closeout and deferred work (2026-11-20)

The Maze's End analysis is wrapping up at the USER's request, so the box can go to a new deck. Ledger:
`docs/design/analysis-Maze's End.md`. Rulings history: `docs/design/mazes-end-work-queue.md`.

USER, 2026-11-20 ~21:30 UTC (VERBATIM): "Yes, at this point we should only do high priority items. Ideally you
would be wrapped up in a few hours so I can start a new deck here. And yes, I realize we won't be able to do the
mulligan profile yet. I'm trying to use the reset budget efficiently which means I need to prioritize
high-inference tasks like analyzing over high-CPU tasks for the rest of the week."

## State at closeout
- **Batch 2 LANDED 2026-11-20 23:02 UTC as f519df877** on the shared branch (push queue; built on origin
  1f3c23fab by cherry-pick). Contents:
  - the overnight GT for the 13 new mazesend keys;
  - DM-2;
  - RC, with the print-only diagnostic;
  - P7;
  - F6: the fed float and the Baldur's Gate hold, both ON;
  - P5d, added on the USER's scope ruling: "Anything crucial or important should go in." That covers the
    fetch-then-replay turn, the Amulet no-drop tier, and the USER's idle-mana prune (a) `MTG_MAZE_IDLE_FETCH`,
    ON. The reveal guard `MTG_MAZE_IDLE_FETCH_REVEAL` is flipped ON. `MTG_MAZE_FETCH_TIER_FULL`,
    `MTG_MAZE_FETCH_RANK_COND` and `MTG_MAZE_MANABOND_DISCARD` stay OFF.

  Handoff: `logs/maze/I6-handoff.md`.
- **Validation on the combined tree:**
  - units 3803/3803, scenarios 594/594;
  - VPC on all 930 refs: 0 drift or divergence;
  - 25 keys moved across 9 decks; accepted set = FAIL set;
  - `check_gt_logs` 1358 consistent, 0 stale.
- **Bars:**
  - ref_bench maze_s_end 0/11 short (5.818 = human);
  - verify_deck GATE PASS;
  - suite cost: mazesend worst shape d5 b20 4977 ms/game, 0.88x Hinata's 5681 in the same regression run
    (limit 3x).
- **d8 b0:**
  - 12 searched slower games: 11 recover;
  - s3003 g112 (seed 3115) is UNRESOLVED: both the combined and origin binaries hit the 50M-unit ceiling;
  - 254 d0 slower games not checked at d8 (d0 is net better in both tiers);
  - P5d's g108 guard-on repro never produced a result (killed after 12.5 h), so that is UNRESOLVED too.

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
| P5d "rev" (reveal guard) | LANDED ON in batch 2; `logs/maze/p5d7/` | passes net held-out; d8 b0: 0 persist, 4 UNRESOLVED (unit ceiling) | shipped ON; the USER may flip it off |
| P5d revq2 | same | passes net; d8 b0: 11 persist (the q2 component) | out |
| P5d q2, cond, c2, q2nb | same | fail the net bar | out |
| P5b land-twin fold, with the RC fix | `wip/maze-rc1-p5b` 3ebba2691; RC2-handoff | std −2 / 2HG −1; the 3 slower games recover at d8 b0 | **ADOPTED by the USER 2026-11-21**; lands in the next gated batch |
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

## USER answers, 2026-11-21 (VERBATIM)
- **Q1 reveal guard: KEEP ON.** "Makes sense. All you did was fix an error in implementing my design if I'm understanding
  this right. Idle mana means idle to the end of the turn, not at one specific breakpoint." (Correct reading: prune (a)
  judged idleness from a plan's own actions and ignored the continuation after a Guild Summit reveal.)
- **Q10 Baldur's Gate conditional hold: KEEP.** "That makes sense. Technically tapping it last can be bad if you don't make
  sure to hold up the mana for it and other times we might only be able to use it effectively for {C} anyway."
  Gap to check later: the hold's condition (`LineDemandAfterPayment`: fed mode gains and the line still owes >= feeder+1)
  reads the remaining DEMAND; it does not reserve the FEEDER sources untapped. The held-out A/B had no unrecovered slower
  game, but a board where earlier payments spend the feeders is not specifically tested. Add a unit case (feeders spent
  -> Baldur's Gate is not held and pays {C}) when this deck is next worked on.
- **Q2 P5b land-order fold: ADOPTED.** "Okay, we can adopt. Folding those makes sense to me." Owed: land
  `wip/maze-rc1-p5b` 3ebba2691's fold (`MTG_FOLD_LAND_ORDER_SEEN` default ON) in the next gated batch: rebase onto the
  shared tip, build, units, smoke + regression, accept the moved keys, d8 b0 on any new slower game. Not landed at
  closeout (no CPU-heavy work this week).
- **Q7 Gods dredge: rule 5 is the target shape.** "The 3+ Gods in the graveyard rule makes no sense. So Rule 5 is the
  closest to what I am imagining. The question is how we should handle any cases where it is falling short. Again my rule
  is that it shouldn't lose except on clairvoyance. I do think that most of the cases where the other rules win are likely
  clairvoyance, but I don't want to assume that. It needs to be determined instead."
  Plan (CPU, deferred):
  1. Held-out A/B on gods d5 b20 + gods2hg d5 b20 (+ d3 b10): rule 5 vs rule 4 (with the deck-out fix) vs rule 3, each
     arm run CLAIRVOYANT and BLIND (`blind_salt`), arms innermost, one pooled batch.
  2. Every game where rule 5 is slower than either other rule: replay at d8 b0 blind with several salts. A loss that
     persists blind is NOT clairvoyance: root-cause it (which condition of rule 5 picked the losing answer) and amend
     rule 5. A loss only under clairvoyance (the other rule's answer wins only for the actual library order) is accepted.
  3. Note: rule 5's deck-out guard (`library <= best_mill + 2 -> {0}`) is the SAME threshold as DMG's
     `kDredgeDeckoutMargin` (leaves < 3 cards), but rule 5 FORCES the draw while the DMG fix ADDS the draw beside the
     dredge. Decide which shape rule 5 ships with from step 2's evidence.
  4. Then switch the default to (amended) rule 5 and delete rule 4's three-Gods natural draw.

## Open USER questions carried forward
3. Build and A/B the bp-node per-host gate? (recommend yes)
4. Manabond q2: prune "yes" when it would discard the held land tutor and no Maze's End is anywhere? (currently it only orders "no" first; recommend prune)
5. Conditional fetch ranking: pursue cond (root-cause its clairvoyant +1/+3), or drop it? (recommend pursue; c2 is out)
6. Burgeoning in 2HG: no measurable effect (std -4/1000, 2HG -1/300); recommend the same treatment in both formats. Matters only if q2 ships.
8. Fixed pay-refine node cap, or F5R's budget-charged cap? (recommend keep the fixed cap until the screen shows Galadriel stays fast)
9. IDG: run its quality-vs-time comparison and root-cause Martyr/Hydras/Merfolk Turns? (recommend yes)

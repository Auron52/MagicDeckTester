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

## USER answers, 2026-11-21 (second set, VERBATIM)
- **Q4 Manabond held-tutor case: PRUNE, try it now.** "Good point, that is a probable hold case, which didn't occur to me
  at the time. We should try it before moving on." Built in batch 3 as `MTG_MAZE_MANABOND_TUTOR_PRUNE` (sub-lever of
  `MTG_MAZE_MANABOND_DISCARD`): the yes is pruned when it would discard a held land tutor with no Maze's End ours.
- **Q5 conditional fetch ranking: try it.** "Okay, we'll try it. We do want to wrap up in the next few hours to start
  another deck, but I do want to clean up some loose ends if possible." Correction: per DECK, P5d-7's cond already met
  the net bar (std clairvoyant -2 / blind -51; 2HG -4 / -24); only single rows (d5 +1, d0 +3) were positive. Its d8 b0
  check was never run. Re-measured in batch 3 on the shipped base (reveal guard ON, fold ON).
- **Q6 Burgeoning: same treatment in both formats.** "I think that's fine. The case it is particularly good is actually
  early (plus to drop a lot of lands) and if you already played manabond it is no longer early."
- **Q3 bp-node hole: close it.** "Okay, if there is a potential hole in our design it should be closed." Implementation
  on `wip/bp-node-host` (lever `MTG_BP_NODE_HOST_GATE`). It moves every deck's budgeted play, so it needs the all-deck
  screen + held-out A/B on the decks that move + d8 b0, best run overnight.
- **Q8 pay-refine cap: follow-up next week.** "How can budgeting things bring back slowness? Why would we ever want to do
  something that does that? Budgeting is meant to be something that helps performance at the cost of quality, so if it
  makes both worse that is a waste of time and effort. Unless that fixed node cap is actually a major hole in the
  design? But even if that case I wouldn't want to make a switch that hurts performance and quality... All that said, it
  seems like this is an item that will need a follow-up. Maybe next week when I'm in less of a rush to use inference
  budget." The explanation owed: the fixed cap applies at EVERY budget (so at unlimited budget a payment refine can stop
  short: a small completeness hole); the budget-charged form is complete at unlimited budget, but the executor's real
  apply runs with no budget, so without its own bound there it would be uncapped (the Galadriel >40 min case). Any switch
  must keep a bound at the executor's apply and must not be slower.
- **Q9 IDG quality-vs-time run: overnight, not urgent.** "I guess we could plan this for a time period while I am asleep
  or something. It seems reasonable to experiment with, but not urgent."

## CHECKPOINT: batch 3 IN PROGRESS (2026-11-21 ~04:50 UTC)

### Branch

`wip/maze-batch3` (worktree `.claude/worktrees/maze-shared`) is REBASED onto shared tip 906d3c0a4 (resolution below).
It is in the push queue, requeued at the back while the fix is measured. Its commits, oldest first:

- **5007342fa:** the P5b fold, `MTG_FOLD_LAND_ORDER_SEEN` DEFAULT ON.
- **482ede8c8:** the tutor prune.
- **3eee6531b:** `MTG_MAZE_FETCH_RANK_COND` + `MTG_MAZE_MANABOND_DISCARD` DEFAULT ON.
- **eca34e3de:** the P5 unit pin for COND.
- **ac15d5ede:** the tutor prune does NOT fire while a Guild Summit of ours is out (the yes's Gates draw).
  - Unit case in `test_mazes_end_p5d6.cpp`.
  - Found on regression std d5 s3003 gi100 at d8 b0. Prune on: T5 (blind T6). Prune off: T4 (blind T5).
    Not clairvoyance; the T3 end-step yes with Summit out draws 3 and wins T4.
- **9fa7468bf:** `scripts/unit_par.sh` plus `docs/design/unit-test-order-dependency.md`. NOT yet usable as the unit
  step, because of the order dependency that doc describes.

**Rebase resolution:** the `HeuristicArm.h` enum and kNames are keep-both, shared slots first;
FOLD_LAND_ORDER appears once. `check_references_safe` OK.

### Validation

**First lease, on the rebased pre-amendment tip.** Build and units passed. Smoke and regression moved only
mazesend keys:

| Tier | Searched slower | Searched faster |
|---|---|---|
| smoke | 8 | 0 |
| regression | 6 | 8 |

All 14 slower games ran at d8 b0 (`b3/m_t8.json`, `b3/t8.out`). 13 recover; gi100 persisted, and the amendment
fixes it. Those smoke/regression runs PREDATE the amendment, so they must be re-run at the next lease.

### A/B

`b3/m_ab2.json`, `b3/ab2.out`, `b3/ab2_tables.txt`, slower games in `b3/slow2.txt`. Binary `b3/mtg.ab2` (= ac15d5ede).
Arms: ship = fold only; cq2 = COND + DISCARD, prune off; cq2ta = the amended defaults.

Net turns per deck, cq2ta vs ship (cq2 is almost identical):

| Deck | Clairvoyant | Blind |
|---|---|---|
| std, all rows | -44 (d0 -30) | -75 |
| 2HG | -2 | -29 |

PASSES the net bar.

### d8 attribution

The 3 clairvoyant persists of the OLD d8 (`b3/d8.out`, cq2t):
- **d3 g120:** recovered on the rebased binary.
- **d5 g224 and 2HG g3:** caused by MANABOND_DISCARD (nodisc = ship; nocond and noprune do not help).
- **Blind d8:** g224 ship = nodisc = cq2ta = T6, so the loss is clairvoyance only (USER rule: OK). g3 blind: ship 6,
  nodisc 6, cq2ta pending in `b3/d8b.out` (job `AT8B_mazesend2hg-d5_g3_cq2ta`).

### RUNNING

The d8 b0 check of cq2ta's slower games, both arms, on `b3/mtg.ab2`.
- **Inputs:** `b3/m_d8b.json`, 392 jobs, 50M-unit ceiling. 76 clairvoyant jobs first, then 310 blind; d0 skipped.
- **Outputs:** `b3/d8b.out`, marker `b3/d8b.done`.
- **Scoring:** adapt `b3/and8.py` (it reads `slow.txt` and `d8.out`) to `slow2.txt`, `d8b.out` and arm cq2ta.

### NEXT

1. **Score d8b.** A clairvoyant persist must be shown clairvoyance-only by its blind run. Blind persists: report them
   (blind d8 is not deterministic-optimal; it has sampled worlds).
2. **If OK:** `push_queue.py join` and a `wait` waiter. At the lease: fetch and rebase, build, units (serial
   `mtg-test` until the unit_par dependency is fixed), smoke + regression under `hold`. Then d8 b0 the searched
   slower games (`mktier8.py` pattern), `--accept`, check_gt_logs, `land`, and a CI watch with `--interval 60`.
   Finally fast-forward the primary checkout and rebuild (the viewer).
3. **If the discard rule fails:** land with `MTG_MAZE_MANABOND_DISCARD` OFF (and so its sub-levers). That is the
   USER's call; surface it.

### Done elsewhere

`wip/bp-node-host` 82dd3a38f (USER Q3), `MTG_BP_NODE_HOST_GATE`:
- units 3807/3807 both ways; Windows CI success;
- repro stale leaf hits 1 -> 0;
- unmeasured (overnight with IDG Q9).

## Open USER questions carried forward
None (all ten answered 2026-11-21).

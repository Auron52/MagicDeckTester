# Viewer: cycling in plans on every deck (QR1) and unpayable one-step lines (Q37)

Branch `cyclone-viewer` (from `cyclone-analysis` ff116556c), worktree `.claude/worktrees/cyclone-viewer`.
USER 2026-10-29: *"The play viewer issues you mentioned should probably be sorted out. Cycling should show in plans
and Seething Song plans should error properly if cast with 1 mana."* Ledger entries: Q37 and QR1 in
`docs/design/analysis-Unpredictable Cyclone.md`.

## What was built (all human-play / viewer only; the autonomous search is unchanged)

**Q37 -- an unpayable cast errors with the board's reason.** The enumeration keeps offering the phantom plan
(TurnSolver's order-free ritual affordability credit is load-bearing as a proposal in the search). Two routes:

* Validation (line builder): `ValidateUnpinnedHumanLine` (src/main.cpp) previews a one-step, land-less,
  single-action line exactly as its commit runs (`PrepareHumanCommit`, empty pin) and makes it ILLEGAL:
  `this line cannot be paid: Seething Song costs {2}{R}; only 1 mana available`. Multi-step lines carry `seq=`
  and were already previewed; an unpinned multi-step line is deliberately NOT previewed (its nulled-chooser trial
  under-reports: `hinata_spasm_hoisted_before_sol_ring`, `bruna_human_karoo_float_legacy`).
* Commit (a plan index picked directly): every drop site reports a reason beside `dropped_casts`
  (`g_play_dropped_reason_sink`, paired with the cast sink -- GameLogger.h `PlayReportDrop` /
  `PlayDropSinkScope`; TurnSolver.cpp `DroppedCostReason`). Decision/result JSON `dropped_reasons`; the viewer's
  rollback banner (`notifyDropped`) prints them; the `**` preview error uses them.
* Generic: reproduces on Cyclone and on Dragonstorm (Seething Song, Pyretic Ritual, Desperate Ritual under the
  viewer's `MTG_UNPRUNED`). Hinata2's Irencrag Feat phantom is not enumerated (stage 2 already rejects it).

**QR1 -- cycling is a plan item on every deck.** `HumanPlayCycleSources` (TurnSolver.cpp): the provider's
`SearchedCycleSources` where it searches its cycles (Cyclone, unchanged), else every distinct hand cycler whose
life half is payable. The cycle-colour hold (`PlanTraits::cycle_seed_pips`) arms under `HumanPlayActive()` too.
Menu indices do not move: generic seeds carry `hand_index = kHumanGenericCycleSeedHand` (-2) and
`OrderHumanCycleSeedPlans` drops the seed-only duplicate of the standalone "cycle X to draw" plan and moves every
seed-bearing plan behind the older ones -- at the end of `AppendHumanPlayDigPlans`, again right before
`AppendBreakpointVariants` on the land-drop path (after the tapland stable_sort; the fan caps base plans in list
order, `MTG_BP_MAXBASE`), and at the end of `EnumerateMainPlans` (behind the COMBO OFF plan).
`AppendHumanPlayLandsEdgePlans` keeps its "pass" base when the only other plans carry seeds.

## Evidence (final binary)

* unit 2108/2108; scenarios 339/339 (6 new: `cyclone_human_song_one_mountain_rejected`,
  `dragonstorm_human_{song,pyretic}_one_mountain_rejected`, `hinata_human_irencrag_one_mountain_rejected`,
  `treasure_hunt_human_cycle_in_line_keeps_blue` -- fails with `MTG_ACT_LINE_HOLD=0` --,
  `treasure_hunt_validate_cast_then_cycle_accepted`; new scenario key `expect_graveyard_contains`).
* `viewer_protocol_check.py --strict`, 678 refs: every reference has the SAME verdict class as the baseline
  binary (250 ok / 427 repaired / 1 beaten / 0 play-drift / 0 enum-gap / 0 contract-fail) -- measured on the
  binary before the last ordering fix; the four sensitive refs (EDF s16/s18/s22, treasure_hunt s8) were re-run on
  the final binary and match baseline in detail too.
* Menu-prefix check (baseline menu == exact prefix of the new menu, full plan JSON, every recorded main frame):
  23 cycling-deck refs + 2 Dragonstorm controls checked PREFIX-OK before the run was stopped for the 13:30
  generation; one MISMATCH, below.
* Viewer layers (everything in `test/viewer_checks.sh` but the protocol sweep): all pass on the previous binary;
  `viewer_save_parity_check` failed ONCE (EDF s1 live-child vs stateless validation, JSON differs past the first
  300 chars) and passed 4 reruns (2 final, 2 baseline) -- intermittent, cause not found. `viewer_validate_check.js`
  (serial, very long) was started and stopped three times for binary changes: UN-RUN on the final binary.

## Open (deferred)

1. **Valve interaction on very wide boards.** When a frame already exceeds the viewer plan-space bound, the seed
   family doubles the position count and the valve can drop one more OLD group (EDF `claude_s5_gi4` T4, replay
   bound 8192: baseline dropped Cloud of Faeries, new drops Cloud of Faeries + Mariposa Military Base; 1437 vs
   1436 plans; the reference still replays with the identical verdict and detail). Fix: in the valve's rank-fill
   (`vranked`, TurnSolver.cpp, "THE RANK-FILL SPENDS ITS LAST SLOTS ON AFFORDABLE GROUPS FIRST"), rank a group
   whose actions are `IsHumanGenericCycleSeed` LAST, so the new family is the first thing dropped and the older
   menu is untouched. Then re-run the prefix check.
2. Finish the prefix check over the remaining cycling-deck refs, `viewer_validate_check.js`, and the final full
   protocol sweep on the final binary.

## Resume commands (from the worktree; box must be free -- 4 threads max)

```
./build.sh
D=/workspaces/MagicDeckTester2/build/scratch/cyclone-viewer; cp build/Release/mtg $D/mtg.final
THREADS=4 MTG_PGO=0 bash test/regression.sh --smoke                       # if not already PASS (play-changed 0)
MTG_BIN=$D/mtg.final python3 test/viewer_protocol_check.py --strict --threads 4
REFS=$(find references/EldraziDisplacerFlicker references/FiveColour references/Fluctuator references/treasure_hunt \
       references/WhiteKnights references/Unpredictable_Cyclone -name 'claude_s*.json' | sort)
python3 $D/t/menu_prefix.py $D/mtg.base $D/mtg.final 4 $REFS              # mtg.base = build of ff116556c
MTG_BIN=$D/mtg.final node test/viewer_validate_check.js
```
`$D/mtg.base` and `$D/t/menu_prefix.py` live on the build volume (regenerable: build ff116556c for the base).

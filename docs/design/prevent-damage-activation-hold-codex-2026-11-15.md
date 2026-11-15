# Prevent Damage activation hold: isolated candidate

This continues mana-clean queue item 10 on `wip/pd-hold-demand`. The shared branch is unchanged while the candidate is checked.

## Reproducer and mechanism

Seed 30027719, turn 6, has 22 APPLY-DROP reports for a line with Dina, Beseech the Queen, and Pyrohemia activations. The oracle pays Dina's {G} with Karplusan Forest, Beseech's {B}{B}{B} with Grand Coliseum twice and City of Brass, and the first Pyrohemia {R} with Battlefield Forge. The current activation hold reserves Karplusan Forest as its first two-colour red provider. Dina then uses a black source for green and Beseech drops. The baseline oracle repro reports 22 APPLY-DROP flags (`build/pd-hold-baseline.err` in the primary checkout).

`MTG_ACT_HOLD_CAST_DEMAND` (default on, `=0` restores the old rank) prefers, among activation-colour providers, a source whose **other** colours the unpaid cast line does not need. Breadth and battlefield index break ties. The hold remains a preference: if the reserved payment fails, the payer still retries unrestricted. A focused unit fixture checks that Forge is held for {R} and Karplusan is free for Dina's {G}.

## Focused validation

The optimized Release build succeeds and the full unit suite passes: 3,133 cases and 3,600,469 assertions. The one-game mana-oracle repro on the **same candidate binary** has 22 APPLY-DROP flags with `MTG_ACT_HOLD_CAST_DEMAND=0` and zero with `=1` (`build/scratch/pd_hold_demand_ab/repro2-{off,on}.err`). The startup flag registry recognizes the lever, and the batch runner has a matching per-job heuristic arm. This is the focused mechanism result; the larger quality gate is still pending.

## Play-quality gate

The full smoke tier covered 207 rows: 206 matched ground truth, and only `bruna_smoke_d0_s1001` changed its digest with the same average. Its two changed games were reviewed against the prior binary. Gi261 had identical draws, one extra damage on T7, and the same T8 win; gi612 had identical draws, skipped a T5 attack, and the same T6 win. Searched smoke had zero changed games and zero slower games. The Bruna depth-zero row was accepted after that per-game review.

The regression tier passed all 327 rows with zero changed games or slowdowns. Strict reference replay: 343 OK, 493 repaired, one beaten, zero play drift, board divergence, or enumeration gaps; both suboptimal references were strictly beaten. `test/check_gt_logs.py`: 1,184 consistent, zero stale or missing.

The one pooled fresh-seed A/B (`build/scratch/pd_hold_demand_ab/manifest.json`, 20,000 total games) compared old and new flags in each job pair. All three cells were outcome-identical and had zero changed play digests: Prevent Damage shipped 3,000 pairs, Bruna depth zero 5,000 pairs, Bruna shipped 2,000 pairs. Every cell had net zero turns, zero faster, and zero slower games, so no depth-eight recovery was needed. This validates play quality on these held-out cells while the targeted seed proves the payment repair.

## Fresh mana-clean gate

The candidate landed on the shared branch as `66f041d49`. The PGO build trained on the regression suite and stamped source tree `91c08095fe03`. The official 5,000-game gate on game indices 32,000–36,999 reports **303 issues** with byte-identical oracle-on/off play (`logs/mana_clean/20261115-042452/report.md`; `test/mana_clean.json`): APPLY-DROP 12, APPLY-DROP-ORDER 4, ORACLE-GAP 90, UNMODELED 8, CAPPED 189. This is a different seed block from the preceding 593-issue gate, so 593→303 is not a paired effect estimate. The original seed 30027719's 22 drops were removed in the same-seed repro above.

Prevent Damage remains in beta. The next fresh block is 37,000–41,999. The remaining APPLY-DROP signatures are chiefly Dina plans whose unseen Rolling Earthquake continuation is dropped (seed 30036366 T5 and seed 30033491 T5); the report's rollback census classifies 10 of 12 drops as need-later. The other categories need separate diagnosis before a clean gate.

On the rebased source `acc8ea6729f9`, the same-binary held-out A/B again found no win-turn changes in 3,000 Prevent Damage, 5,000 Bruna depth-zero, or 2,000 Bruna shipped pairs; Bruna had one digest-only change in each setting. The full unit suite passed 3,173 cases, and smoke/regression passed 211/333 rows with no win-turn changes. The next fresh PGO gate, indices 37,000–41,999, found 274 issues with oracle-on/off play identical: 46 APPLY-DROP, 4 APPLY-DROP-ORDER, 65 ORACLE-GAP, 6 UNMODELED, and 153 CAPPED (`logs/mana_clean/rebased-queue-20261115/report.md`). The rollback census now has 44 depth-1 drops and two capped; this is a different seed block, not a paired issue-count improvement. Prevent Damage stays beta, and its next fresh block is 42,000–46,999. A remote Merfolk solver change arrived after this gate began, so this result is tied to the recorded source stamp.

The later combined source `24d8b2638f8a` includes that Merfolk solver change. All 3,176 unit cases passed. The full smoke/regression batch had zero searched win-turn changes; six smoke and five regression rows had score or digest mismatches against committed ground truth, confined to White Knights, Goblins depth zero, and one Merfolk Turns same-score line. Twenty paired one-game replays of the changed seeds gave identical outputs with `MTG_ACT_HOLD_CAST_DEMAND=0/1`, including the one Goblins depth-zero slowdown. The old snapshot replay helper could not parse the new `Islandwalk` keyword in the current card data, so its explanatory diff was unavailable. Strict viewer replay passed 844 references with zero drift/divergence/enum gaps, and `check_gt_logs.py` reported 1,205 consistent. The repeated 10,000-pair held-out A/B (`build/scratch/pd_hold_remote2_ab/summary.json`) again has zero faster/slower games in all three cells and the same two Bruna digest-only changes. These tier mismatches therefore belong to the incoming source change, not the activation-hold flag; their ground truth was not accepted by this mana queue.

The fresh PGO gate on that combined source, indices 42,000–46,999, found 370 issues with byte-identical oracle-on/off play: 20 APPLY-DROP, 6 APPLY-DROP-ORDER, 88 ORACLE-GAP, 11 UNMODELED, and 245 CAPPED (`logs/mana_clean/postmerfolk-queue-20261115/report.md`). All 20 payment drops are depth-1 in the rollback census. This is a new seed block and not a paired count comparison with the prior gate. Prevent Damage remains beta; its next fresh block is 47,000–51,999.

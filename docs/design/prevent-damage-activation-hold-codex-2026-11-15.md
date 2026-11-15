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

## Remaining gate

- Land the reviewed candidate on the shared branch.
- Build PGO, run a fresh 5,000-game Prevent Damage mana-clean block (expected 32,000–36,999), and record the result. The deck leaves beta only when a fresh block reports zero issues.

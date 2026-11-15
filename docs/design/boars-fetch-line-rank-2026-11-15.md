# Fetch ranking by selected cast line (2026-11-15)

Status: proposed, default **off** (`MTG_FETCH_RANK_PIPS=1` enables it). The named sole-target fix is separate and always on.

The candidate ranks each legal fetch target against the coloured costs in the actual cast sequence that follows that target. A spell still in hand is never counted as a mana source. It also retains the hand-aware pip and land-coverage rules as tie-breakers, so a two-Birds line can still take Stomping Ground for a later red spell. The solver enumerates all legal target/line combinations, keeps every distinct cast line, and lets the provider choose only among plans identical except for the fetched land. Human play still shows every legal target.

Providers with reviewed fetch rules opt out of this generic ranking. The flag-on unit sweep exposed Spirits' Strand priority being reordered on the held-crack route; Spirits and Mill now explicitly retain their own fetch orders.

Paired 300-game cells on the integration source based on `510d0684b`, using the same seed and budget on both arms (`build/scratch/boars/line_rank_ab/manifest300.json`). The later Mill rebase does not change these decks' providers, but the ranking remains off until reviewed:

| Cell | Saved turns (on vs off) | Faster | Slower |
|---|---:|---:|---:|
| Boars play | 6 | 6 | 0 |
| Boars d3/b10 | 2 | 5 | 3 |
| Boars 2HG play | -1 | 2 | 3 |
| Boars 2HG d3/b10 | 2 | 3 | 1 |
| Creature Giving play | -2 | 0 | 2 |
| Creature Giving d3/b10 | 2 | 2 | 0 |
| Creature Giving 2HG play | 1 | 2 | 1 |
| Creature Giving 2HG d3/b10 | 1 | 1 | 0 |
| Minotaur play | 1 | 1 | 0 |
| Minotaur d3/b10 | 0 | 0 | 0 |

Ten slower game indices were rerun with five *blind search* salts each, preserving the realized shuffle. The two Creature Giving play losses (gi125, gi138) tied in all ten blind comparisons; CG 2HG gi258 tied in all five. Boars 2HG play gi126 and gi269 and Boars d3 gi166 and gi173 likewise ceased to show a consistent loss. Boars d3 gi84 and Boars 2HG d3 gi42 and play gi22 still favored off on that fixed realized shuffle. They first differ in Forest versus Stomping Ground and then draw different cards.

For all ten indices, 20 fresh realized shuffle salts with blind search gave these nonzero aggregate turn deltas (on minus off; an unwon game scored turn 10): Boars 2HG d3 gi42 +1; Boars 2HG play gi126 -3 and gi269 -3; Boars d3 gi84 +1 and gi166 -2; CG 2HG play gi258 +2. The other four indices tied. The three Forest-versus-shock fixed-shuffle losses therefore did not reproduce as consistent losses across realized shuffles.

CG 2HG gi258 is a distinct search judgment problem already traced in `docs/design/boars-codex-handoff.md`: taking the red fetch makes War-Riders castable, and search sometimes selects it instead of the legal two-Birds line. At depth 8 with unlimited budget under blind search, the latter remains legal but is not reliably selected. The line-aware ranking does not resolve that judgment. Do not use its raw paired loss as proof that a fetch target is unavailable, and do not turn the candidate on before reviewing that behavior.

Artifacts: `build/scratch/boars/line_rank_ab/{ab300.err,off300.wins,on300.wins,cmp300.txt,manifest_slow_blind.json,slow_blind2.err,manifest_slow_real.json,slow_real.err}` in the primary build volume. On the final Mill-rebased source, all 96 fetch unit cases and 4,237 assertions passed with `MTG_FETCH_RANK_PIPS=1` (`build/boars_rebased_mill_fetch_on_fixed.log`).

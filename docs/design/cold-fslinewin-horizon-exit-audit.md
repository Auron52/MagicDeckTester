# Cold single-pass FSLineWin callers vs the first-verified-win exit (DEFERRED audit)

**Status:** deferred 2026-09-27. Found while sizing the Pirates value leaf; the one confirmed instance
(the lazy-leaf probe) is FIXED (`MTG_LAZY_LEAF_LADDER`, default ON). The rest is an unverified audit.
**2026-10-02:** the ladder is now unconditional (the `MTG_LAZY_LEAF_LADDER=0` single-probe arm was deleted),
and `MTG_LAZY_LEAF` itself is default ON for unlimited budgets (USER), so every unbounded run uses it.

## The rule being violated

`FSLineWin`'s first-verified-win exit (`FsHorizonExitOn`, the `tail.win_turn <= state.turn_number +
depth - 1` returns in FSLineWin / FSLineTail) returns on the FIRST tail that wins at or before the
horizon edge. Its own comment: sound only because "a pass runs only after every shallower pass found
no win ... so any in-horizon win is at that edge = the global minimum ... NOT a standalone
earliest-win finder." Any caller that runs ONE cold FSLineWin at depth D with no refuted d1..D-1
passes beneath it can commit a later in-window win that merely came first in move order.

## Confirmed instance (fixed)

The lazy-leaf probe (`MTG_LAZY_LEAF`, armed on every unbounded H cell of the value-leaf matrix since
2026-09-21) ran one cold pass at the full depth. Pirates, H5 unbounded, seed 8008: 14/40 games T4 ->
T5 (avg 4.775 vs 4.425); gi1 committed a T5 line at T1 after 6 search units. `MTG_FS_HORIZON_EXIT=0`
restored all 14. Fix: the probe is now a leafless ladder 1..D sharing `leafless_cache`; 40/40 games
per-game identical to lazy-off H5, at 20.5 vs 51.9 core-s/game. The error direction is always
"deeper looks slower", i.e. depth tables pessimistic about depth.

## What is still open

1. **Giants: AUDITED 2026-09-27 -- the bug IS present in its matrix; the shipped model's adoption
   still stands.** One pooled batch, current binary (`2ee0ea85`), the generation's own H5 jobs (staged
   model, `budget_ms 0`, `max_turns 8`, abandon settings unchanged), seeds 8008/9009/10010/11011 x 100
   games, `MTG_LAZY_LEAF_LADDER=0` for the whole batch, per-job `flags: {MTG_LAZY_LEAF: false|true}`:
   * broken probe vs lazy-off: **15 of 400 games T4 -> T5, 0 the other way, +0.0375 turns** -- the
     Pirates signature. Worse on all 4 seeds (5.75/5.74, 5.67/5.63, 5.71/5.68, 5.57/5.50).
   * fixed ladder (`MTG_LAZY_LEAF_LADDER=1`) vs lazy-off: **0 of 400 differ, play digests identical on
     all 4 seeds** -- the fix is exact on Giants too (and is the control that the arms can differ).
   What the biased H cells fed in `decks/Giants/Giants.value.json`:
   * `value_trust_depth_candidate: 5` -- inert: trust ships UNSET (candidate byte-identical 8/8).
   * `value_fallback_crossover.take_heuristic_at_hdepth [1,1,1,3,6,6,6,6]` -- LIVE in play, and
     derived from H-vs-V per depth, so a pessimistic H4/H5 biases it toward keeping the leaf.
   * The adoption itself was measured at BUDGETED play, where the lazy leaf self-disables: -0.00312t,
     t=-4.08, 7/0/1 seeds vs live at 0.14x. So the shipped model is a real improvement; what may be
     left on the table is a better crossover, not a regression.
   **Follow-up (USER: not before Monday 2026-09-28; one batch at a time):** re-run the matrix's H arm
   (H2-H5; the matrix shows H3=H4=H5=6.0137, so H3 may be affected too) on the SAME freeze
   (`ac4b8965`, Rule 0) with the lazy leaf OFF -- answer-identical to the fixed ladder, just ~2.5x
   slower -- which keeps the frozen commit instead of mixing binaries. Then re-run phase D to
   recompute the crossover and trust candidate, and A/B the revised sidecar against the shipped one
   at play settings. Only if it wins does anything change (and then check the Giants mulligan, which
   reads `value_play`).
2. **Other decks' tables built with the broken probe.** Every value-leaf H cell generated with `MTG_LAZY_LEAF=1`
   from 2026-09-21 until this fix, at depths whose window reaches past a decision's earliest win, may be
   biased. Adopted in that window: Giants (above) and **Fungus** (`decf3d5a`, 2026-09-22, from the
   2026-09-21 regeneration). Fungus's provenance note says its `value_leaf_table`/crossover are "the OLD
   ones until the regenerated matrix lands", so its live crossover (`[1,1,1,1,3,5,6,6]`) may predate
   the lazy leaf -- CHECK which matrix produced it before re-running anything. Same audit shape as
   Giants: the H5 jobs, broken vs lazy-off vs fixed, one pooled batch (~10 min on a free box).
3. **Other cold callers (hypothesis, not checked):** the value pass at a committed depth (~49207, fresh
   `vcache`), ~50124, ~49653/49892, and the `MTG_ESC_JUMP` jump ladder (skips intermediate passes).
   For each: does it run FSLineWin at depth D without the shallower passes refuted? If so, either
   ladder it or scope the exit off for that call, and A/B per game.

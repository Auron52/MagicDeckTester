# Cold single-pass FSLineWin callers vs the first-verified-win exit (DEFERRED audit)

**Status:** deferred 2026-09-27. Found while sizing the Pirates value leaf; the one confirmed instance
(the lazy-leaf probe) is FIXED (`MTG_LAZY_LEAF_LADDER`, default ON). The rest is an unverified audit.

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

1. **Tables built with the broken probe.** Every value-leaf H cell generated with `MTG_LAZY_LEAF=1`
   from 2026-09-21 until this fix, at depths whose window reaches past a decision's earliest win, may be
   biased. The Giants value leaf (adopted 2026-09-23) is the live one to re-audit: a 12-game d5 spot
   check found no difference, which is not proof. Re-measure its H4/H5 cells per game, lazy on vs off.
2. **Other cold callers (hypothesis, not checked):** the value pass at a committed depth (~49207, fresh
   `vcache`), ~50124, ~49653/49892, and the `MTG_ESC_JUMP` jump ladder (skips intermediate passes).
   For each: does it run FSLineWin at depth D without the shallower passes refuted? If so, either
   ladder it or scope the exit off for that call, and A/B per game.

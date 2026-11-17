# Breakpoint continuation: inherit the opener flag only while an outer opener is pending (2026-11-17)

Lever: `MTG_CONT_UNSEEN_PENDING` (heurarm slot `CONT_UNSEEN_PENDING`), DEFAULT ON; `=0` restores the old
inherit-always behaviour byte-for-byte.

## The defect

Dragonstorm mana-clean gate `codex-dragonstorm-ruby-song-20261117`, seed `30075408` T4, plan
`{Rite of Flame + Desperate Ritual + Seething Song + Apex of Power}`: APPLY-DROP x6 (seven with the two-Apex
variant). The line cracks Lotus Bloom for `{B}{B}{B}`. Apex of Power's continuation casts Ruby Medallion, then
Dragonlord Kolaghan `{4}{B}{R}`.

A line that casts a breakpoint opener (`CastOpensUnseen` = `ParamKeyedDrawClass`: Apex, Treasure Hunt, ...) sets
`g_line_opens_unseen`, because its hold is only a lower bound on what the turn will owe. While it is set, the
line-aware generic orders stand down (`LineGenericOrderLive`) and the float drain uses its historical WUBRG order.
The continuation inherited the invoking line's flag because that line casts Apex. So the drain paid Ruby's `{2}`
with the `{B}`s, and Kolaghan was dropped with ten `{R}` floating. The oracle keeps one `{B}` floating.

But Apex had already resolved by then: its reveal is exactly what the continuation was solved on. The
continuation's own list IS the rest of the turn, unless an outer opener is still uncast.

## The change

`ContinuationOpensUnseen` (`TurnSolver.cpp`) replaces the inherit-always read at both continuation bindings: the
search's `cont_line_armed` and the executor's `ExecBindContinuationLine` (`AIEngine.cpp`; all three call sites).
The committed-line replay already uses the stamped `rec_line_hold`. A continuation keeps the inherited flag only
when an opener cast of the invoking list is still PENDING, which takes both of these:
- the card is still in hand (or in the graveyard, for a graveyard cast);
- its cost is still in the invoking line's unpaid bill (`g_line_unpaid_cost`).

The bill test tells the line's own second copy apart from a spare one. s30075408 holds a second Apex that the plan
does not name, and a by-name hand check alone kept the flag (7 -> 7). Example of a legitimately pending opener: the
second Apex of a two-Apex line, or the second Treasure Hunt of TreasureHunt's s3003 gi96 line.

A section whose own flag was inherited passes it on unread (`g_line_unseen_carried`,
`LineUnseenCarriedScope`), because its invoker's list is not the outermost one. The continuation's OWN openers
still set the flag as before (`LineOpensUnseenCasts`).

## Measurement

Exact seed: APPLY-DROP 7 -> 1 (lever off 7). The remaining flag is the two-Apex plan, where the first Apex's
continuation correctly stays unseen.

Paired A/B, one pooled Release batch, both arms per job via the heurarm flag. Dragonstorm 5,000 pairs, every other
suite deck 1,000, games 530,000+ (`build/scratch/claude_cont_unseen_ab/`):

| deck | earlier | later | digest-only | work units |
|---|---|---|---|---|
| Dragonstorm (5,000) | 1 (gi 534771, T5 -> T4) | 0 | 0 | -0.12% |
| Accomplished Alchemist | 0 | 0 | 2 | +0.00% |
| Cyclone | 0 | 0 | 2 | -0.04% |
| Niv | 0 | 0 | 2 | +0.03% |
| the other 52 suite decks | 0 | 0 | 0 | within +/-0.00% |

No game is slower. Dragonstorm's score total is 21,742 vs 21,743 (lower is better). Unit suite 3,393/3,393.

Tests: `test/unit/test_cont_unseen_pending.cpp` covers five cases:
- a spare Apex with no bill share does not keep the flag;
- the bill holding Apex's cost keeps it and marks it carried;
- an inherited flag is passed on;
- the continuation's own opener sets it;
- the lever off restores inherit-always.

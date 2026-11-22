# The greedy chooser's lethal projection ignores lords cast this turn (deferred, 2026-11-22)

Status: DEFERRED (found while root-causing the one slower game of `MTG_EVAL_COMBAT_TOKEN_SELF`; not built).

## The defect

`TurnSolver::Solve` (the d0 runner and the playout beyond the horizon) scores every payable subset and lets a
projected win dominate (`wins`, TurnSolver.cpp, the `projected_atk + ... >= opponent life` test). `projected_atk`
is `PendingAttackDamage(state)` (the attackers already on the battlefield, read with their LIVE power) plus haste
casts, prowess, Vial haste and a few deck-specific terms. A lord or anthem the subset casts BEFORE combat
(Drogskol Captain, Supreme Phantom: "other Spirits you control get +1/+1") pumps every attacker this turn, but
nothing adds that pump to `projected_atk`. A subset that is lethal only through such a lord does not read as a
win, so it competes on `total_eval` alone.

## The game that shows it

Spirits d0, seed 1412341, game_index 2341 (`MTG_EVAL_COMBAT_TOKEN_SELF` A/B, 2026-11-22). T4 m1, opponent at 12,
three Spirits ready to attack for 8:

| subset | proj_atk | real damage | eval (lever off) | eval (lever on) |
|---|---|---|---|---|
| {Drogskol Captain, Supreme Phantom (Vial)} | 8 | 14 (lethal) | 1900 | 1900 |
| {Millicent, Mausoleum Wanderer, Supreme Phantom} | 8 | 11 | 1800 | 2000 |

Neither subset reads as a win. With the lever off the lethal one wins on eval by luck; with the lever on,
Millicent's own token credit (+200, which is correct) lifts the non-lethal one above it, and the game is won on T5
instead of T4. Repro: `MTG_SOLVE_PROJ_TRACE=4 MTG_EVAL_COMBAT_TOKEN_SELF=<0|1> mtg decks/Spirits/Spirits.cod
--profile decks/Spirits/Spirits.profile.json --ignore-play-profile --depth 0 --budget-ms 0 --games 1 --seed 1412341
--game-index 2341`.

## Sketch of a fix

For a pre-combat subset, add to `projected_atk` the power each cast lord/anthem grants each counted attacker
(subtype filter, the "other" exclusion, double strike doubles it). The precise route is to put the subset's lord
casts on a scratch copy of the battlefield and re-read `PendingAttackDamage`. That costs a copy per subset, so
gate it on the subset holding a lord (a per-candidate flag computed once) and on a cheap upper bound (attackers x
summed lord bonus) reaching the opponent's life. `EnumeratePlans` has the lockstep twin of this projection.

This is a play change for every lord deck (Spirits, Merfolk, Goblins, Elves, Slivers, ...), in the d0 runner AND
in the searched playout, so it needs its own held-out A/B over those decks and d8 b0 replays of every slower game.

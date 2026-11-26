# The greedy chooser's lethal projection ignores lords cast this turn (deferred, 2026-11-22)

Status: the BOARD half is built as `MTG_ATTACK_LORD_CAST` (default OFF since 2026-11-22: fails the every-deck bar on
Boars / Goblins d0, USER call). The HASTY-CAST half is built and unconditional since 2026-11-26 (section at the end).

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

## 2026-11-26: the plan's own HASTY casts (Elves d0 gi3558) -- built, unconditional

`MTG_ATTACK_LORD_CAST` covers a lord cast this turn pumping the attackers ALREADY on the board. The other half was
never credited by anything: a lord cast in the same plan pumping the plan's own HASTY casts. Each hasty cast's
`Action::haste_attack_power` is stamped once in CollectActions with the PRE-PLAN board's lord bonus, and
`ProjectPlanWalk` erased every walked body before reading the board (its comment called that "the conservative
direction"). Two more holes sat beside it: a multikicker / squad cast face ("<name> (Kicked xK)", emitted by its own
loop in CollectActions) never received the haste stamp at all, so a kicked Joraga Warcaller or a squadded Galadhrim
Brigade under Concordant Crossroads projected NO attack; and the walk entered the printed card without the face's
enter counters or squad copies.

**The game.** Elves d0, seed 9803559, game_index 3558, T4 m1, opponent at 18; board Priest of Titania, Elvish
Archdruid, Concordant Crossroads, three Forests. {Elvish Clancaller, Elvish Archdruid, Leaf-Crowned Visionary, Joraga
Warcaller} deals 18 (four lords, each +3 from the other three, the Warcaller +4; both dorks tap to pay). It read
proj_atk 9 at the 8-supply payment and 11 at the grown supply, so no subset read as a win and the sum-of-eval rank
picked {Immaculate Magistrate, Clancaller, Archdruid, Warcaller}: 12 damage, win T5. Repro:
`MTG_SOLVE_PROJ_TRACE=4 build/Release/mtg decks/Elves/Elves.cod --profile decks/Elves/Elves.profile.json
--ignore-play-profile --depth 0 --budget-ms 0 --games 1 --seed 9803559 --game-index 3558`. Fixed: the unkicked set
reads 20 and the same set with the Warcaller kicked once reads 22; both read as wins, the greedy plays the kicked one
(higher eval), the executor's attack deals exactly 22 (opponent 18 -> -4), and the game is won T4 (was T5).

**The fix (TurnSolver.cpp).** `HardCastHasteCreature` / `HardCastHastePower` are the one haste stamp, now applied to
the multikicker / squad faces too (a squad face is 1 + K bodies). `BuildAttackTapCtx` marks lord casts (`kWalkLord`)
and hasty casts (`kWalkHaste`) when a lord cast is on offer beside a hasty cast (or `MTG_ATTACK_LORD_CAST` is on);
`AttackTapLoss` walks a subset that holds both. `ProjectPlanWalk` keeps each cast lord on the walk board TAPPED (it
grants its static bonus, never attacks), puts a Warcaller face's K counters on it, enters a squad face's K token
copies, and credits every hasty walked body `ComputeLordBonus(walk board) - ComputeLordBonus(pre-plan board)`. Without
`MTG_ATTACK_LORD_CAST` the kept lords are erased before the board is read, so the board's attackers are priced as
before (that half stays the USER's call). No flag: it is the estimate made correct, in the permitted greedy (d0
runner, playout beyond the horizon) and in the searched enumerator's lockstep projection alike.

**The over-read it exposed: a creature sacrificed for mana still attacked.** The first held-out A/B of the change
above was net +7 on Goblins d0 (2 slower of 5000: gi275 3 -> 6, gi1895 4 -> 8; seeds 9900001+gi). Both are the same
pre-existing defect. Goblins d0 gi275 T3, opponent at 19: {Goblin King, Skirk Prospector, Skirk's "Sacrifice a
Goblin: Add {R}"} read 19 -- every Goblin on the board still swinging -- while the payment sacrificed both Goblin
Lackeys and Muxus, and the attack dealt 12. The plan's own lord credit (King pumping the hasty Skirk, +1) is exactly
correct; it tipped a projection that was already 6 too high over the line. `AttackTapLoss` priced a creature TAPPED
to pay, never one SACRIFICED to pay. Fixed in the same stage 1: every SacForMana activation that eats a creature
(`IsSacManaOutlet` -- Skirk Prospector, Utopia Mycon -- or a creature that sacrifices itself for mana, Generator
Servant) removes its victim from the stage-1 board before the attack is read: the baked victim of a single sac,
else `CanonicalSacVictim` (the pick `ApplySacForMana` makes), one per sac of a burst. A baked victim that another
activation of the same plan already took, or a source that an earlier activation sacrificed, still costs a body
(the line counts its mana). Both Goblins games now win T3 (gi1895 a turn FASTER than base). Memo key: the stage-1
entry and the stage-2 memo carry the subset's sacrifice bits. This is very likely also what failed
`MTG_ATTACK_LORD_CAST` on Goblins d0 (+177): a more complete lord credit on top of the sacrificed bodies' attack.
Not re-measured here (the lever stays the USER's call). Test: "F3d sacrifice for mana".

**Not covered (still under-reads, conservative):** a haste GRANT cast in the same plan (Goblin Chieftain / Warchief
cast this turn hasting the other casts); double strike granted by a same-plan lord; Marwyn's counters from the
plan's entering creatures; replicate copies' lord share.

Tests: `test/unit/test_attack_projection.cpp` "F3c lord stack" (the lands-only lord stack reads exactly 14, the
gi3558 board reads as lethal at 18 and the greedy plays it; kicked Warcaller x2 reads 4 / 6 with LORD_CAST off /
on; a squadded Brigade reads 6).

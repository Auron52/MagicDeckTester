# Play defects exposed by the 2026-10-10 discard round (DEFERRED)

Status: DEFERRED. Found while root-causing every game the USER's cleanup-discard designs made slower
(the pooled A/B `logs/discard_impl/ab1` and the smoke / regression tiers at c658e3c2). In each case below
the discard itself was correct or irrelevant; a pre-existing play defect decided the game. Sections 3 and
4 now have default-ON fixes behind their own levers (see their Status notes); the rest are not fixed.
Each changes play outside the discard and needs its own A/B. Investigation logs are under
`logs/discard_impl/rc/<deck>/` (gitignored, local to this checkout).

The ritual-ordering executor defect (Dragonstorm, `MTG_PAYABLE_ORDER`'s domain) has its own doc:
`docs/design/executor-ritual-payable-order.md`.

## 1. SelesnyaLifegain, depth 0: a phantom Nykthos Paragon plan

`selesnya_regression_d0_s2002` gi 344 (seed 2346). From turn 4 to turn 8 the depth-0 runner's plan is
"cast Nykthos Paragon" ({4}{W}{W}) every turn; the executor taps nothing and casts nothing, so the
ground-truth game cast nothing for five turns while holding a Priest of Titania and two Genesis Waves.
The solver judges the Paragon payable and the executor cannot pay it. Unconfirmed guess: Selesnya
Sanctuary's `produces ["G","W"]`, `produces_amount 2` read as two flexible G/W mana where the executor
pays one colour. Owed: trace the solver's affordability verdict against the executor's payment.

**ROOT-CAUSED AND FIXED (branch fix-selesnya-d0, lever `MTG_COLOR_EXACT_SCALED`, default ON; pooled
A/B owed).** The guess was half right. Board T4-T8: Sanctuary, Branchloft Pathway (G face), Llanowar
Elves, Wellwisher, an untapped Elvish Archdruid -- ONE white source. Solver side: the flat pool books
the Karoo's fixed {G}{W} as two WILD (`AddSourceToPool`, `SpellEffects.h` ~26127), so `{g5, wild2}`
passes `pool.CanPay({4}{W}{W})`; `SubsetPayable` is a presence test (a {W} source exists); and the one
gate that prices a Karoo exactly, `BuildColorFeasibility`, returned `usable = false` for the WHOLE board
because an untapped scaled mana dork (the Archdruid) was on it (`ManaPayment.cpp` 3553 at c77cffb5).
Payment side (correct, per the card): `TapSourceIntoFloat` (`ManaPayment.cpp` ~560) and the
backtracker's `bundle_src` (`SpellEffects.cpp` 3931) add exactly one {G} and one {W}; the flow oracle
(`SpellEffects.cpp` 2440) proves it infeasible, so nothing is tapped or cast. Fix: the gate no longer
stands down for a scaled dork or a live Wirewood Lodge burst; it credits them an unbounded count in
their own colours (their yield is unbounded within a plan, their colours are not) and stays exact for
everything else. `MTG_COLOR_EXACT_PROBE=2` over 100 d0 games: 24 rejects, 0 false rejects.

## 2. SelesnyaLifegain, depth 0: a non-mana {T} ability is not discounted from the attack

Same game, policy arm, turn 8: the plan "Genesis Wave X=4" was projected lethal (6 damage - 4 for the
creatures tapped for mana = 2 = the opponent's life), but Wellwisher also tapped (its own {T} lifegain),
so nothing could attack. `consider()` subtracts only creatures tapped FOR MANA (`AttackTapDiscount`,
`src/ai/TurnSolver.cpp` ~7549 and ~32483-32497); a creature tapped by a non-mana {T} activation in the
same plan is not. SelesnyaLifegain does not opt into the board-lethal short-circuit
(`DecisionProvider.h` ~1574). Inferred from the arithmetic, not traced. Fix sketch: count {T} non-mana
activations in the attack-tap loss.

**FIXED (branch fix-selesnya-d0; pooled A/B owed), and the arithmetic above was wrong in one place:**
the mana discount did not subtract 4 -- it subtracted nothing. Its `need` was `cost -
pool_noncreature.Total()`, and `BuildNonCreaturePool` is the pool for NONCREATURE SPELLS (it keeps every
mana dork), so need = 7 - 7 = 0 and the projection read the full 6. That discount has been dead on every
board whose dorks finish the payment since it was added (92c7ce07). Two levers, both default ON, both
in `AttackTapLoss` (`TurnSolver.cpp`, used by Solve's and EnumeratePlans' `consider`):
`MTG_ATTACK_TAP_NEED_EXACT` (need = cost beyond every source that is not an attacking creature mana
source; 0 post-combat) and `MTG_ATTACK_TAP_ABILITY_DISCOUNT` (re-project the board with the plan's
{T}-activated attackers tapped). Either alone still reads the T8 plan as lethal.

Repro game (gi 344): policy OFF 8 -> 6, policy ON unwon -> 6 (each fix alone: 7 / 7; all levers =0
reproduces the old games byte-identically). 100 d0 games (seed 600001, paired): -8 turns, 7 better /
0 worse.

## 3. EldraziDisplacerFlicker, depth 0: an unverified projected win outranks the verified combo route

A/B gi 671 (seed 217872, turn 7): the go-off route trial WON (19 draws) and was on the menu, but the
runner took a cast plan whose projected win was optimistic and unverified (`[edf-goff-cut] t7
projected=1 verified=0`). `Solve` ranks plans by (projected win, value, mask) (`TurnSolver.cpp` ~31692);
the Gorge branch's projection (`DecisionProviders.cpp` ~33532) is not trial-verified like the combo
route (~26830-26843). Also: the auto go-off count is sized as opponent life / Gorge damage (10, ~27039)
and returns before the dig sizing that wanted 19; red was one-shot, so the Gorge fired once.

**Status (2026-10-10): fixed behind two default-ON levers, pooled A/B owed.** Root cause of the ranking:
the ComboRoute action carried no win in Solve at all (eval 999, no damage term) while every cast plan
on a Gorge board PROJECTED a win, so any cast plan worth >= 1000 buried it. `MTG_SOLVE_VERIFIED_WIN`
(TurnSolver.cpp, `route_win_j`): a plan holding the trial-WON route is a win, a verified win dominates a
projected one, and the route is scored alone before the walk. The sizing: `MTG_EDF_GORGE_RED_SIZING`
(DecisionProviders.cpp, `GorgeRedEachIteration`): the Gorge's life/damage count returns early only when
the untap set renews a resource-free red source (Aether Hub's {E} red is one-shot); otherwise it is a
floor under the drain / exile / draw-land sizing. gi 671: 8 -> 6 (either lever alone gives 6).

## 4. EldraziDisplacerFlicker, depth 0: three-turn freeze

A/B gi 1261 (seed 218462): the policy arm casts nothing on turns 6-8 although Fertile Ground and Cloud of
Faeries are castable; the generic arm froze the same way on turns 4-5. A fixture of the real turn-6 board
freezes with either hand. Which guard picks the empty plan was not found.

**Status (2026-10-10): root-caused and fixed behind `MTG_SOLVE_PENDING_AURA_EXACT` (default ON), pooled
A/B owed.** The plan was not empty: Solve chose {Eldrazi Displacer} ({2}{W}, eval 300) on a board with no
white source (Aether Hub without energy, Mariposa, Kitchen). The flat pool counts the Kitchen dual as
`wild`, and both colour gates (ComputeAvailableColors, BuildColorFeasibility) are widened by every land
Aura still in HAND (PendingLandAuraColorMask) -- the Fertile Ground made {W} "available" to a subset
that never casts it. EnumeratePlans can afford that over-admission (a rollout replays the plan); Solve
cannot (its pick is executed), so the executor failed to pay and cast nothing, every turn. The lever
re-asks both gates without the in-hand credit and rejects a failing subset unless it casts a land Aura
and passes the real payer (SubsetPayableWithFilters). gi 1261: 9 (loss) -> 6; generic arm 8 -> 6.
It is generic Solve code, so it also touches Bruna and Fungus (Wild Growth).

## 5. Search: tied win turns are broken by scan order

Two independent slower games came out of a 1-turn tie the root broke by enumeration order (the root
replaces its best only on a strictly earlier win, `TurnSolver.cpp` ~57387):
- EDF A/B gi 63/163 (seed 217164, d3 and shipped settings): turn 3, Fertile Ground + Wild Growth and
  Fertile Ground + Cloud both scored win-turn 6; the Cloud line is scanned first and was a turn slower
  (real 6 vs the other line's 5). The discard made the Cloud line's playout MORE accurate.
- Melira `melira_regression_d3_s2002` gi 33 (seed 2035): turn 1, "Darkbore Pathway as Slitherbore" and
  "no land" both scored 7; Slitherbore is scanned first and strands Carrion Feeder (no black), true value
  7 vs 6. Persists at 16x budget (depth-bound, not budget-bound).
Owed: a principled tie-break between equal win turns (e.g. the leaf value, or the earlier-turn damage /
resources), measured on the suite.

## 6. EldraziDisplacerFlicker, search: the playout's land drop ignores a needed colourless source

In the EDF gi 63 playout the greedy land drop played an enters-tapped Conservatory on turns 7 and 8 and
never Mariposa Military Base while Essence Depleter (which needs {C}) was in play. A rollout-policy issue
(greedy beyond the horizon is permitted; its quality is still ours to fix).

**Fix built, pending the pooled A/B (2026-10-10, branch `fix-playout-land-colour`).**
`MTG_ROLLOUT_LAND_ACT_COLOR` (default ON; `=0` byte-identical): the rollout's greedy drop
(`SimulateLandPlay`, `TurnSolver.cpp`) first asks `ActivationColorLandIndex` (`LandPlay.cpp`) for a land
supplying a colour (W/U/B/R/G/{C}) that a permanent in play needs for an activated ability and that
nothing on the board makes; a legacy pick that already supplies it is kept. Shared rollout policy, not
the EDF provider: the rollout drop has no provider hook and the rule is deck-agnostic. Measured at
c77cffb5 + the fix: gi 63's turn-3 Fertile Ground + Cloud playouts went from 10 stranded-{C}
Conservatory drops to 0 (12 Mariposa), but the game's win turn is 6 in both arms (the root's tail
verdicts are unchanged); 100 EDF d3/b10 games (seed 600001) are play-identical (same digest, 430 turns
both arms; only search units moved), and `=0` matches the pre-change binary exactly. Not covered: a
{C} sink cast THIS turn (the drop precedes the cast that brings it), the executor's own greedy drop
(`GreedyLandChoiceIndex`) and the enumeration tiebreak that names it.

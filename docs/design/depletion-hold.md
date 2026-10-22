# Depletion lands: hold if you can, use otherwise (`MTG_PAY_DEPLETION_HOLD`)

Status: **built, DEFAULT OFF, adoption is the USER's.** The cross-deck held-out A/B is prepared
(`scripts/depletion_hold_ab/`) and has not been run (the box was reserved for a mulligan generation on
2026-10-22). This page is the standing record of the rule, what it does at each payer site, and what is
still owed.

## The USER's words (2026-10-22, NivMizzetCombo seeds 1 and 6, in order)

1. *"Another bad set of mana usage. Depletion Lands should go late if possible"* -- seed 6, a T3
   Brainstorm paid with Saprazzan Skerry's LAST depletion counter.
2. *"As for depletion lands we probably need smarter rules as much as I hate to say it."*
3. *"The smartest rule for depletion lands would hold the depletion land if it can do so and use it
   otherwise. At least, it should hold it over non-creature source that are not consumed."*
4. *"And I do realize this is difficult to do with good performance, but that will be the goal."*

## Why the earlier attempt was not enough

`MTG_PAY_DEPLETION_WASTE` / `_ANY` (0689d31a6, default off) demote a depletion tap whose second mana this
payment and the rest of the line cannot use. It fixes seed 1, but the seed-6 analysis
(build/scratch/niv/agent_seed6/, on an older binary) found the Skerry tap it demotes had kept Spirebluff
Canal free for a {R}{R} later that turn -- both waste flags made that line T5 instead of T4. A per-pip "is
this tap wasted?" test cannot see that the land it spares is the line's second red source; the rule below
asks the rest of the line (unit test: the seed-6 shape).

## The rule

"If it can do so" is a question about the REST -- the rest of this payment AND the rest of the line --
so it is answered once per payment, not per pip:

* `DepletionHoldMask` (ManaPayment.cpp) finds the largest set of untapped depletion lands that the
  remaining demand can be paid WITHOUT, from the sources the payer ranks ahead of a held land.
  Colour-exact: Hall's condition over the demanded colours (each source credited its colours and its
  per-tap yield; a Karoo one of each; a filter or ramp filter one net unit only while something else
  can feed it). The set is built in HOLD order -- fewest counters first (a last counter is the land
  itself), then the later battlefield slot -- the reverse of the greedy's spend order, so the lands it
  leaves out are exactly the ones the payment spends anyway.
* A held land is ranked behind every plain-tier source; a land the rest needs keeps its old rank (11,
  mono + the depletion nudge), so it pays FIRST and the source it would otherwise have spared is not
  tapped as well. With two depletion lands and one dual, {2}{U} can do without either land but not
  without both: the set holds the last-counter Needle and the Skerry pays with the Canal.
* Demand that the board cannot meet at all (a line funded by a mid-line producer such as Izzet Signet,
  a payment only the backtracker can chain) holds nothing -- the historical order.

## What it does at each payer site

| site | with the flag on |
|---|---|
| per-cast greedy (`TapForCostSharedOnce` scarcity loop): single-cast plans, every cast of a plan the prepay declined, activations | the set is computed once per payment attempt, lazily: only when a depletion land with counters is about to WIN a pip against an alternative (a basic ranked ahead of it, or the land being the only source, costs nothing); if that land is held the pip is selected once more with the band live. The attempt's own reservation is not supply. A held land joins a band at `key[2] + 128`: past the whole plain ladder (0-59) and the pain band (+64), still `key[0] == 0`, i.e. ahead of the 60+ reserve tiers, the creature band and §2b fodder |
| a fed filter's feeder choice (same function, kind-2 tap) | a held land feeds last (same band) |
| whole-turn prepay (`TurnSolver::BatchPrepayMainCasts`) | a PARTIAL depletion rung `[creatures + one-shots + held set]` right after "hold everything"; before, the depletion class was all-or-nothing and its release let the joint solve tap depletion lands in battlefield order |
| backtracker (`TapForCostBacktrack`) | unchanged: per cast it runs only when the greedy fails; in the prepay it carries the rung's reservation |
| affordability checks (flat pool, ColorFeasibility, the cast-order walks) | unchanged: the hold is an order, never a filter, so payability is the same; the sequential walks run the real payer and so see the order, with the line hold bound exactly as in play |
| rollout vs executor | one shared payer and one prepay function -> lockstep by construction |

## Order relative to the other source classes

* **Creatures** (mana dorks, Generator Servant's body): unchanged -- the creature band (64+, `MTG_DORK_TAP_LAST`) is still behind a held land; a held land is still spent before a dork.
* **Reserve tiers** (colourless manland 60, scaled land/dork 61, storage land 62, sole-colour / Lodge 63): unchanged, still behind.
* **Consumed sources:** rituals' and sac-for-mana float is spent before any tap (unchanged); §2b fodder (500+) unchanged. **A Treasure / pay-sac one-shot (rank 26) now pays BEFORE a held depletion land** -- it sits in the plain tier between the duals (20) and the tri lands (30), so "behind every non-consumed land" cannot keep the land ahead of it without also moving the Treasure. The one-shot hold (`MTG_ONESHOT_RESERVE`, prepay class + single-cast held attempt) still holds the Treasure first wherever the payment can do without it.

## The USER's seeds (single games, niv-deplete build, flag off -> on)

* **Seed 1**, T3 `land=Spirebluff Canal; cast: Tandem Lookout` (plan 73; the T3 menu is identical in both
  arms). OFF: {2}{U} taps Sandstone Needle's last counter + Saprazzan Skerry, floats {R}, the Needle dies,
  the Canal stays up -> the search plays the rest to **T7**. ON: Skerry + Canal, the Needle keeps its
  counter -> **T6** (at d3 b10 and at the play setting alike). The T3 board as a scenario
  (agent_seed1's `scn_lookout_single_cast.json`) pays the same way.
* **Seed 6**, T3, the user's `cast: Brainstorm` (OFF plan 44 = ON plan 45 -- the flag ADDS one T3 plan,
  "Preordain, Izzet Signet, Brainstorm, Brainstorm, Servant sac", which the sequential payability walk now
  pays, so every later index shifts by one). OFF: the Brainstorm takes the Skerry's last counter. ON: the
  Canal pays it and the Skerry is held; the search's T3 continuation then spends the Skerry on
  Niv-Mizzet, the Firemind (where the line needed it). **T5 in both arms.** (The earlier finding that
  the Skerry tap kept the Canal for a later {R}{R} and won T4 was on an older binary; on this tree the
  same line is T5 either way.) `cast: Izzet Signet, Brainstorm` (OFF 41 / ON 42) pays identically.
* A Niv reference replay check is owed before adoption: plan indices shift where the flag makes an extra
  order payable (above).

## Cost (the agent's perf probe, 2026-10-22)

One pooled batch, both arms, the A/B's two cells per deck at 5% of its size and on its own seeds
(430000 / 440000, so the A/B's games stay unseen), this build alone on the box at 20 threads: 15,614 games,
**core-s 1.001x and search units 1.002x** on/off over everything. Where play moved: th 55 of 700 games
(units 0.98x), galadriel 31 of 700, mirrorwing 9 of 600, dragonstorm 5 of 700, niv 3 of 25 (units
1.03x), kitty 1 of 350; angels, critter, whiteknights, bruna, breaching and fungus moved NO game (their
decks pay mostly off basics, which already rank ahead of the depletion land, so the hold rarely has
anything to reorder -- a reading of the decklists, not yet checked game by game). Win turns in the
probe: 6 games faster, 1 slower (th) -- far too few games to read.
The held set costs one board pass plus a Hall check, and is computed only when a depletion land is about
to win a pip; no extra payer pass (one re-selection of the pip at most), and no extra search node by
construction (the search is untouched; units move only through the different payments).

## Owed

* The cross-deck held-out A/B: `bash scripts/depletion_hold_ab/run_ab.sh` from a tree built with
  `./build.sh` (12 depletion decks x {d3 b10, play b20}, flag off vs on, paired, seeds 500000 / 600000,
  581,400 games, ~74k core-s -> ~61 min at 20 threads saturated, ~65-75 min with the tail, under the box
  lock; report in `logs/depletion_hold_ab/report.txt`). The adoption bar is per deck: net <= 0 on the
  paired aggregate, and every slower game root-caused.
* If adopted: smoke/regression GT moves for the decks that move (accept through the normal flow), and the
  Niv references need a replay check (a payment change can shift plan menus, see seed 6 above).
* Not covered by the rule (it sees the board, never the future): a line whose later casts are drawn
  mid-turn (a breakpoint) or paid by a mid-line producer (Izzet Signet) -- there the demand is not payable
  from the board and nothing is held (the historical order).
* The land drop's ORDER ("go late") is a separate, searched decision. `DecisionProvider::LandPlanTieKey`
  orders the drop only among EQUALLY-scored plans and keys on whether the drop is played up front, not on
  WHICH land; a provider could use it (it sees the plan's actions) to prefer a non-depletion land on a
  tie, but that is a provider tie-break the USER would have to ask for -- not built.

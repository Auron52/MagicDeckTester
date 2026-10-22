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

## The cross-deck A/B and Mirrorwing's slower games (2026-10-22)

The A/B ran (581,400 games, report `logs/depletion_hold_ab/report.txt` in the niv-deplete worktree). Every
deck is net <= 0 except **Mirrorwing: +0.0003 (t +1.41)** -- d3 8 faster / 13 slower, play 10 faster / 16
slower, every one by exactly one turn. Scratch for what follows: `build/scratch/niv/agent_mirrorwing_hold/`.

**All 29 slower games replay exactly** (single-game chunks, digests equal to the A/B's `.wins`).

**Mechanism (27 of 29): a cantrip paid off green to keep the red-only Sandstone Needle.** Every trick this
deck casts on its kill turn draws (Fists of Flame {1}{R}, Ancestral Anger {R}, Oracle's Restoration {G}), so
the line hold names the cantrip and nothing after it; the rest of the turn is the cards it draws, and they
are as often green (Ignoble Hierarch, Gold Rush, Fortifying Draught, Elvish Mystic) as red.
`DepletionHoldMask` sees a payable {1}{R} without the Needle and holds it; the cantrip is paid with Game
Trail / Forest / Rootbound Crag, and the drawn green spell then finds only the Needle's {R}{R} (or must tap
an attacking dork). Executor traces, identical board in both arms:
* play gi=16230 T4 -- OFF: `Fists of Flame tapped: Sandstone Needle` -> Hierarch (Forest) -> Gold Rush
  (Game Trail + Forest), kill. ON: `Fists of Flame tapped: Game Trail Forest`, then
  `Gold Rush tapped: Ignoble Hierarch Sandstone Needle float{r1}` (the Needle's last counter spent anyway,
  one {R} wasted, the Hierarch tapped), the second Hierarch paid with a Treasure -> no kill, T5.
* play gi=16349 T5 -- OFF: `Ancestral Anger tapped: Sandstone Needle float{r1}` -> Restoration (Game
  Trail) -> the drawn Elvish Mystic (the dork). ON: `Ancestral Anger tapped: Game Trail` -> Restoration
  (Elvish Mystic) -> no green for the drawn Mystic. T7 -> T8.

**It is not budget churn.** A `--scenario` built from the OFF arm's own board at the start of its win turn
(`mkscn.py`): the OFF payer wins that turn at `--budget-ms 0`, the ON payer proves no win at any budget in
26 of 29; from the diverging turn's board, 27 of 29 (the other two, d3 9592 and d3 19294, recover at budget
0 from the diverging board -- budget churn). Whole games at 4x budget (d3 b40 / play b80): 2 of 29 recover
(d3 19294, play 16087).

**Treasures are not involved** (the USER's Gold Rush hypothesis): in all 29 slower games, both arms, no
payment cracks a Treasure while a Sandstone Needle stays untapped, and the amendment below -- which never
touches the Treasure order -- recovers all 29 byte-identical to OFF. The one Treasure spent in a slower game
(16230's second Hierarch) is downstream of the green shortage.

**The 18 faster games are the mirror image**: from the ON arm's board, the ON payer wins its turn at
budget 0 and the OFF payer cannot in 14 of 18 -- the hold paid an early cantrip with a one-mana land and
kept the Needle's {R}{R} for a RED spell later in the same turn (d3 7459: Restoration (Forest), Anger
(Crag), Fists (Needle)). Which order wins depends on the colour of cards not yet drawn; a board-only payer
cannot know it.

## Amendments (prototype, niv-deplete2; every flag DEFAULT OFF and inert without MTG_PAY_DEPLETION_HOLD)

* `MTG_PAY_DEPLETION_HOLD_UNSEEN` (all decks) and its per-deck form, the provider hook
  `DecisionProvider::DepletionHoldNeutralWhenUnseen` -- Mirrorwing opts in via `MTG_MW_DEPLETION_UNSEEN`.
  While the applying line casts a breakpoint opener (`g_line_opens_unseen`, the classifier the float guard
  already uses), the pips a held land could have paid (generic + its own colours) must be covered by sources
  making no colour the land cannot make (a Mountain for the Needle) -- the hold may move a red pip between
  red sources, never spend a dual's or a Forest's other colour on it. A line with no opener is unchanged.
  Mirrorwing: all 29 slower games AND all 18 faster games become byte-identical to OFF (126 / 93 turns vs
  ON's 155 / 74); a random 40 of the games the hold moved without changing the win turn: 37 back to OFF's
  digest, 3 stay ON's. So for Mirrorwing the amendment is the hold switched off in practice -- its lines
  almost always contain a cantrip and it has two Mountains. For NivMizzetCombo it would answer seed 6 the
  opposite way from the USER's ask (a Brainstorm, the Canal's {R} is a colour the Skerry cannot make), which
  is why the per-deck form exists; seed 1 (no opener) keeps T6.
* `MTG_PAY_DEPLETION_FULL_YIELD` (the USER's refinement: *"spend the depletion first if you need both of its
  mana"*, *"primarily for Treasures or other expendable resources"*): a held land whose whole yield this
  payment uses ranks ahead of pay-sac one-shots (`IsPaySacSource`). Mirrorwing: fixes none of the 29 slower
  games (no Treasure contention there) and turns one faster game back to OFF -- play gi=1654 T3, where the
  hold cracked the two fresh Gold Rush Treasures for Fists of Flame and kept the Needle's {R}{R} for the two
  Ancestral Angers Fists drew; with the refinement (and with OFF) that T3 kill is unfindable from the board
  at budget 0 and depth 2/3/5, though the mana is the same -- reason not isolated (owed).
* `MTG_PAY_DEPLETION_HOLD_RETRY` (the USER's strict-dominance retry): a held land that paid anyway with mana
  floating at payment end -> re-pay once with it unheld, keep that only if the spent set is a strict subset
  (`DepletionRetryDominates`). Mirrorwing's 47 games: the trigger never fires (0, search included). Niv seeds
  1/6: fires 389-2766 times per game inside the search, 48-100% of retries taken; never in a real payment;
  win turns unchanged (T6 / T5). The shape (`MTG_PAY_DEPLETION_HOLD_TRACE=2`, seed 1, inside the search):
  `cost {2} ... leftover{r1} tapped: Sandstone Needle Spirebluff Canal | untapped: ... Izzet Signet` -- the
  Hall check counted Izzet Signet fed by the Canal as the Needle's replacement, the greedy spent the Canal
  directly on the first {1} and then reached for the held Needle, floating its second {R}; the Needle alone
  pays {2}.
* `MTG_PAY_DEPLETION_HOLD_TRACE=1` prints each real payment's held set (`[dhold]`) and the counters at exit
  (`[dhold-count]`); `=2` also prints the first float triggers anywhere (`[dhold-float]`).

Not built (USER-deferred): the full-yield rule against LANDS ("float the extra"). Of the 27 mechanism games,
13 open the kill with Fists of Flame paid by the Needle (11 of them exactly, {R}{R} for {1}{R}) -- the shape
that rule targets; 12 open with Ancestral Anger {R}, where the Needle's second {R} floats into a later
spell's generic, which a this-payment full-yield test would not see; 2 are other shapes.

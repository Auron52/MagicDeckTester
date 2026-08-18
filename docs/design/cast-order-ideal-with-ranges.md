# Cast order: an ideal order with affordability RANGES, searched only to stay castable

> ## STATUS UPDATE 2026-08-18 (latest): MIRRORWING and ANTI-LIFEGAIN orders ADOPTED default-on.
>
> Both review gates were held and both packages flipped the same day, each with the full
> measurement ladder (train tiers + held-out per-deck keys) green:
> * **`MTG_MW_ORDERED` DEFAULT ON** — the full reviewed Mirrorwing order. Held-out: 11 green /
>   1 flat / 0 red, per-game 96 faster / 8 slower (both searched slower = budget churn).
> * **`MTG_AL_ORDER` + `MTG_ORDER_RECHECK` DEFAULT ON** — the reviewed Anti-Lifegain order plus
>   the Remedy/Silence alternation. The risky-wipe replacement term is scoped to the CAST-TIME
>   guard (search-committed chains execute; the greedy never initiates — two measured d0-red arms
>   taught the scoping, see cast-order-rankings.md). Held-out: 7 green / 5 flat / 0 red,
>   per-game 12 faster / 0 slower.
> GT rebaselined in all three modes for both decks. `MTG_ORDER_M1_FIRST` remains default-off
> (measured neutral; flipping it churns 7 decks' digests = a full-suite rebaseline incl. a full
> overnight — schedule deliberately). Still open: the Mirrorwing LAND two-position rule at d0;
> the remaining decks' review gates.
>
> ## STATUS UPDATE 2026-08-18 (later): the MIRRORWING review gate was HELD.
>
> The USER reviewed and ruled the full Mirrorwing order (recorded verbatim in
> `cast-order-rankings.md` under "Mirrorwing Dragon"): bodies (magnets, creatures, Libation,
> Twinflame) before draws, draws before Fists, Draught last, Gold Rush on a deterministic
> FUNDING ladder (after-draw preferred, earlier only while the line cannot pay), and — the
> structural ruling — "order everything, not have search own the order". Built behind
> `MTG_MW_ORDERED` (default off, byte-identical off): provider ranks, opaque rank-sort adoption
> hook (`OrderOpaqueCastsByRank`), the multi-rung/funding generalization of the range ladder
> (`CastOrderFallbackRanks` + Treasure credit in `FirstUnpayablePos`), and supersession of
> `MTG_MW_CANTRIP_ORDER` (the measured −20.0 draws-first arm is DEAD under this ruling — bodies
> before draws; re-measure from scratch). OPEN under this ruling: the LAND two-position rule
> (start of turn / after draw) is not yet encoded at d0; arm G's numbers do not carry over.
> Other decks' review gates remain un-held.
>
> ## STATUS: ON HOLD as of 2026-08-18 (paused, not abandoned; nothing is half-applied)
>
> **The tree is in a clean parked state.** All four levers are DEFAULT OFF and byte-identical off
> (`MTG_IDEAL_ORDER`, `MTG_ORDER_RANGE`, `MTG_ORDER_OPAQUE`, `MTG_MW_CANTRIP_ORDER`), so the shipped
> engine plays exactly as if this project did not exist. Nothing needs undoing to resume, and nothing
> needs finishing to leave it parked.
>
> **What is measured.** Steps 1-3 below are built and measured on the TRAIN tiers. Best arm is
> **G** = `MTG_ORDER_OPAQUE` + `MTG_ORDER_RANGE` + `MTG_MW_CANTRIP_ORDER`: **-0.00346 smoke /
> -0.00312 regression**, additive across decks (dragonstorm -57.0, mirrorwing -20.0, hinata -6.0,
> one deck +1.0). `MTG_ORDER_OPAQUE` alone is the confident part (-0.00246, almost all dragonstorm);
> every arm includes it, because without it the rest has no domain.
>
> **What is NOT done, and why it is parked here rather than anywhere else:**
> 1. **The held-out overnight was never run.** Train-tier numbers cannot carry an adoption claim --
>    and on this repo's recent evidence they are actively misleading: a favourable mean has pointed
>    at adoption three separate times while the configuration was wrong (see
>    `colour-blind-subset-affordability.md`). Run `--overnight` and read the PER-DECK keys, not the
>    aggregate.
> 2. **The USER review gate has not been held.** Cast order, affordability range, and 1st-vs-2nd main
>    are a PER-DECK judgement the user signs off; they are never adopted from a measurement alone.
>    The proposal to review is `cast-order-rankings.md`, regenerated with
>    `mtg <deck> --cast-order-report` (it reads each deck's real provider at the default config, so
>    it cannot drift from play). **This review is the gate. Do not adopt without it.**
> 3. Step 3's hoped-for effect -- breakpoint-fan rollout calls falling back toward the 1,625 baseline
>    once the order is unambiguous -- was never measured.
>
> **Two traps recorded from the sweep that produced these numbers:** a lever that EARLY-OUTS looks
> exactly like one that fires and finds nothing (count the times it fires, not just the delta); and
> never run two suite runs of the same MODE concurrently -- they share `test/logs/<mode>/` and
> `test/results/<mode>.env`, so the second clobbers the first.
>
> **Resume by:** rebuild, re-run the train tiers to confirm the numbers still reproduce on the
> current engine (the mana-affordability work in `mana-affordability-arc-handoff.md` landed after
> these were taken and touches cast order's neighbourhood), then the overnight, then the review.

**USER design (2026-08-17, verbatim):**

> A more complete design could be:
>
> 1. An ordered list of all spells, where spells that impact affordability are given a range.
> 2. Spells marked as 1st main or 2nd main.
>
> 2 chooses the natural position of spells. 1 overrides it and decides the order. We only search
> order in order to ensure our ideal order can be cast and choose a less ideal order if not. So, in
> other words, the following principles:
>
> 1. Draw before playing land or rituals.
> 2. Cost reducers before spells they reduce.
> 3. Cards with positive abilities before cards that give them a payoff.
>
> Are the ideal, but they can't get in the way of what is actually possible. This means we need a
> way to choose an alternative for exactly these rules.
>
> Essentially, what it comes down to is that drawing cards, good triggered abilities and mana
> generating/cost reducing effects all want to be cast early and genuinely compete for order. We
> should always start with the ideal order and fall back to something less than ideal, but that is
> functional. That is the only reason for the range I mentioned above. It should be from
> ideal -> cost-efficient.

This completes the single-consideration arc (`single-consideration.md`). Step 1 partitioned the turn
by PHASE (main 1 / main 2). Step 3 partitioned it across a mid-turn DRAW
(`breakpoint-phase-classification.md`). This one settles the remaining axis -- ORDER -- and it is
the axis that has been paying for a search all along.

## What the engine does today, and the bail-out at the centre of it

Order is a static rank sort, not a search:

* `DecisionProvider::CastOrderRank(state, def)` -- LOWER = earlier. Generic ranks are creatures 10,
  noncreature spells 20, on-cast self-damage sources 30 (last). Archetypes override (Anti-Lifegain
  ranks its enablers 0 so a same-turn payload sees the flip -- principle 3, already, for one deck).
* `CastOrderLess` (ManaPayment.cpp) stable-sorts by that rank, then cheapest-first **among mana
  accelerants only** (the Dragonstorm ETB-order lesson: cost is the wrong key for creatures).

And then:

* `OrderingOpaque(name)` -- if the set contains a draw spell, a staging card, cascade, retrace,
  impulse-exile or a solo-target trick, **the reordering is skipped entirely** and the set keeps its
  canonical order, on the stated grounds that "the optimal cast ORDER around it is
  situation-dependent ... a static rank can't capture it. The search owns the ambiguous ordering."

That sentence is the whole cost. Handing ordering to the search around a draw is what makes the
breakpoint class fan out, and the breakpoint fan is what doubles the rollouts (measured: 1,625 ->
3,275 rollout calls on hinata seed 4009 gi=5 at budget 10). **The USER's principle 1 says the draw's
position is not ambiguous: it is FIRST.** Draw before playing the land, draw before rituals -- so the
land and the ritual are chosen with the information the draw provides. Once that is a rule, the set
does not need the search to own its order; it needs ONE re-solve after the draw, which is what the
breakpoint already is.

## The design

### Part 2 -- natural position (exists)

`ClassifyMainPhase` already assigns each cast Main1 / Main2 / Both. That is the "natural position".
Nothing new; it is the outer partition and the order rules operate WITHIN a phase.

### Part 1 -- the ideal order, as ranks

The three principles are rank relations, not situations:

1. **Draw before land drop and before rituals.** A cantrip resolves into information; a land drop
   and a ritual are commitments made better with it.
2. **Cost reducers before the spells they reduce.** Ruby Medallion / Hinata's static before the
   discounted casts (the engine already models the same-turn discount in
   `SameTurnReducerGenericCredit`; this makes the executed line realise what the enumeration
   projects -- the stated purpose of `CastOrderRank`).
3. **Enablers before payoffs.** A card whose ability the turn's later casts cash in goes first.
   Anti-Lifegain's rank-0 enablers are this rule, hand-written for one deck; it generalises.

These three classes -- draws, positive-ability enablers, and mana/cost effects -- are precisely the
ones the USER identifies as "genuinely competing for order". Everything else has a forced position
and is considered once, where it lands.

### The RANGE, and why it is the only thing worth searching

A spell that **impacts affordability** does not get a position, it gets a range:

```
[ ideal position ...................... cost-efficient position ]
```

The ideal end is where the principles want it. The cost-efficient end is where it must go for the
turn to be payable at all. The resolution rule is the USER's:

> We only search order in order to ensure our ideal order can be cast and choose a less ideal order
> if not.

So: **build the ideal order, attempt payment, and on failure walk that spell down its range toward
the cost-efficient end** -- and no further, and only for spells that have a range. A spell with no
affordability effect has a single position and is never re-placed. This is a directed fallback
ladder, not a permutation enumeration: the search over order exists solely to keep the line
castable.

Two properties that make this the single-consideration answer rather than another prune:

* It is **not lossy in the sense the USER's standing bar forbids**. Nothing castable becomes
  uncastable: the ladder's terminal rung is the cost-efficient order, which is the one that pays.
  What is deleted is the set of orders that are neither ideal nor necessary -- permutation
  duplicates in outcome.
* It **considers each spell once** in the common case: the ideal order is attempted first, and a
  spell only moves when payment actually failed, which is a fact, not a guess.

## The boundary: an order is not a COMMITMENT past a draw

**USER 2026-08-17:** "Note that this optimal cast order still doesn't prevent us from reconsidering
spells or lands we drew."

This is the line between "considered once" and "reconsidered because new information arrived", and
it is the whole point of principle 1 rather than an exception to it. The draw goes first precisely
SO THAT the land drop and the rituals are decided with what it found. Retiring `OrderingOpaque`
(step 3 below) therefore removes the ORDER fan around a draw -- it does NOT commit the turn's line
before the draw resolves, and it does not remove the post-draw re-solve.

What is settled once, and what is re-decided, after a draw:

| | after the draw |
|---|---|
| a cast already in hand AND already payable pre-land | settled -- its position was decided, it is not re-offered |
| a card the draw ADDED | fully reconsidered, entering the order at its own ideal position |
| the LAND DROP | fully reconsidered -- a drawn land is playable, which is why the draw preceded it |
| a cast that only the new land can pay for | fully reconsidered (it was not payable when the order was built) |

That table is exactly the partition already implemented in
`breakpoint-phase-classification.md` / `MTG_BP_CLASSIFY`, and the code honours it structurally
rather than by rule: `CollectActions` skips lands before the filter is reached
(TurnSolver.cpp:3667), so a land can never be dropped, and both filter arms require
`BpCardWasInHandBefore`, so a drawn card can never be dropped. The two designs are the same
statement seen from two sides -- order settles what we HAVE, and new information reopens exactly
what it touched.

## What this replaces

* `OrderingOpaque`'s blanket bail-out for draw-containing sets -- superseded by principle 1 (draw
  first) plus the existing post-draw re-solve.
* The breakpoint continuation ORDER fan (`AppendBreakpointVariants` emitting W ranked variants).
  With the draw at a known position and the rest re-decided once with full information, the
  continuation is a single decision, not W ranked guesses. This is where the 2x rollout cost goes.
* `MTG_CANTRIP_ORDER` (canonical cantrip ordering) becomes a special case of the general rule --
  cantrips are ordered by the canonical rank because they are draws, not by a bespoke ban list. Its
  drawn-card exemption stays necessary and is already implemented
  (`breakpoint-phase-classification.md`).

## Worked example: Light Up the Stage, where all three axes are ONE decision

USER 2026-08-17, in sequence: "spectacle is messier, because it gets cost reductions by going after
the first damage spell" / "it also likes to sometimes go second main" / "for the cost reduction" /
"the difficult part is that it is better main 1, but only if spectacle is enabled" / "the reason
being prowess and drawing into haste."

Light Up the Stage is `{2}{R}`, Spectacle `{R}`, and exiles two cards playable *until the end of
your next turn*. Every axis this document separates turns out to be the same question for it:

* **Order.** Its cheap mode is enabled by a PRIOR cast that dealt damage. So its ideal slot is not
  "first" (principle 1) -- it is "after the first damage source", which is principle 3 (enabler
  before payoff) pointing at a COST rather than an effect: the burn spell enables the cheap draw.
  Ranking it by its cheapest cost, as a first attempt did, puts it exactly where spectacle is off
  and it costs the full three.
* **Main phase.** Combat damage also turns spectacle on, so main 2 buys the discount for free --
  which is why it "likes to sometimes go second main".
* **...but main 1 is better when spectacle is ALREADY enabled**, because the exiled cards are
  playable THIS turn: casting it in main 1 leaves this turn's mana to actually spend them, and it
  feeds combat two ways -- prowess triggers before attackers are declared, and a haste creature
  among the exiled cards can attack the turn it is found. Casting it in main 2 wastes both.

So its position is CONDITIONAL on a prior cast in the same main, and the m1/m2 answer depends on
the order answer. A scalar rank cannot express that, and neither can a main-phase label chosen
independently of the order. This is the concrete case that motivates carrying a DEPENDENCY on the
range rather than only a span of positions, and it is why the review (below) covers order, range
and m1/m2 together rather than one at a time.

Status: NOT implemented. The engine currently ranks it by printed cost at the cost-efficient end,
which is safe (it never claims spectacle is live when it is not) but leaves the good line to the
search. The prowess half is already modelled elsewhere -- `MainPhaseFilterActive` stands the whole
m1/m2 filter down when a prowess attacker is available, the fix for burn's round-1 regression where
Main2-classified bolts starved Monastery Swiftspear.

## THE REVIEW RULE (USER 2026-08-17)

> "I think, as a rule, the order and range + 2nd vs 1st main should be reviewed by the user."
> "But ideally, AI or coded rules would help also to come up with a good approach, so that the
> review is easy."

Order, range and main-phase assignment are a per-deck judgement the USER signs off, not something
adopted from a measurement alone. The engine's job is to PROPOSE a defensible ranking from coded
rules and card data, and to make the review cheap -- surface the derivation and flag the uncertain
calls, so what is reviewed is a handful of judgements rather than the whole card list.

`mtg <deck> --cast-order-report` is that proposal, and `docs/design/cast-order-rankings.md` is it
for all decks. It prints rank, range, main phase and the rule that decided each, straight from the
deck's real provider, so the reviewed table cannot drift from what plays.

## What step 2 turned out to be, and why step 3 is not optional

**Step 2 has no domain on its own, and that is structural rather than a matter of tuning.** The
ladder was first wired at the two CLEAN-set sort sites -- the only places the engine reorders casts
today -- and measured ZERO invocations. The reason is a fact about the card pool, not about the
decks: `IsIdealOrderCantrip` is a subset of `IsIdealOrderDraw`, and `IsIdealOrderDraw` is
`OrderingOpaque` plus `cast_draw` / `cast_reorder`. Checked over all of `cards.json`: **11 cards are
promoted, and every one of them is `OrderingOpaque`.** The single promotable card whose only
draw-ness is `cast_draw` is Magma Opus, at mv 8 -- excluded by the discernment rule. So a promoted
card is *by construction* in a set that keeps its plan order, and the ladder can never fire.

Hence step 3 ships alongside step 2. Two things it is NOT:

* It is **not** narrowing `OrderingOpaque` so cantrip sets take the clean branch. The clean branch
  has no breakpoint handling ("No draw engine here, so no breakpoint handling is needed") -- routing
  a Ponder set through it would delete the post-draw re-solve, which is the opposite of principle 1.
* It is **not** a change of MEMBERSHIP. The same casts happen; only their sequence moves. The
  enabler pre-pass, the Spectacle hoist, the staging break and the trailing sac/graveyard loops are
  all untouched, in both worlds.

What it is: the opaque path's "rest in plan order" loop becomes "rest in ladder order"
(`MTG_ORDER_OPAQUE`).

### The ladder, as built

`ApplyCastOrderRangeLadder` (ManaPayment.cpp, shared by executor and rollout):

1. Read both ends of every ordered cast's range. Return if none has a span -- the common case, and
   the early-out that keeps this off the hot path.
2. Return if the set's costs cannot be PROJECTED. `LadderProjectable` declines a producer (ritual
   float / rock ramp), a dynamic cost ({X}, Hinata/Soulfire per-target discounts) -- the same
   fungibility question `BatchPrepayMainCasts` asks -- and, additionally, a **not-yet-live spectacle
   cost**, which is order-DEPENDENT and therefore the one thing a stamped per-cast cost cannot
   describe. That is the Light Up the Stage gap below, declined rather than guessed at.
3. Sort at the IDEAL end and project the line against `AvailableManaPool`. If it pays, done.
4. Otherwise walk down the ranged spell CLOSEST to the failure (the failing cast itself is a
   candidate when it has a range) and re-project. One step per spell, so the walk terminates at the
   all-cost-efficient rung -- the order the engine used before the promotion existed.

The projection is approximate exactly as far as `AvailableManaPool` is (an aggregate pool, not a
per-source solve). That is safe *here* in a way it would not be at a payment site: every rung is an
order the engine would have executed anyway, so a wrong projection picks a different LEGAL order,
never an illegal one.

`MTG_ORDER_RANGE_PROBE` reports entries and skips as well as demotions, because "the ladder never
fired" and "the ladder fired and the ideal order paid" look identical in play and mean opposite
things -- the first measurement above was the former, and only a probe that counts entries could
say so.

## MEASURED (smoke, seed 1001, vs committed GT) -- the promotion is the weak half, not the range

Per-game deltas, weighted by each case's game count over all 15,875 smoke games (negative = better).
Every arm includes `MTG_ORDER_OPAQUE`, without which none of the rest has a domain.

| arm | levers | per-game | verdict |
|---|---|---|---|
| B | `ORDER_OPAQUE` | **-0.00246** | ordering the opaque set is a real win on its own |
| C | + `IDEAL_ORDER` | **+0.00151** | the promotion is a net LOSS |
| D | + `ORDER_RANGE` (producer-declining ladder) | +0.00151 | ladder inert -- identical to C on every key |
| D2 | + `ORDER_RANGE` (producer-aware) | **-0.00038** | the ladder repairs most of the promotion |
| F | `ORDER_OPAQUE` + `IDEAL_ORDER` + `IDEAL_CANTRIP_MV=1`, **no range** | -0.00094 | the ladder's isolation arm |
| E | F + `ORDER_RANGE` | **-0.00265** | best arm: mv bar + range, both needed |

**Two suite runs of the SAME MODE must never run concurrently** -- `test/logs/<mode>/wins/` and
`test/results/<mode>.env` are fixed per-mode paths, so a second run clobbers the first's per-game
output and both arms read back a mixture. Two arms were invalidated that way here (the tell: a
case reporting `got=(no output)`, and a digest appearing in one arm that belongs to the other).

Per key (delta = got - exp; "churn" = play changed, avg identical):

* **B**: burn churn x3; hinata d0 **-0.0060**, d3 +0.0067, d5 +0.0133; dragonstorm d0 **-0.0340**,
  d3 -0.0066, d5 churn.
* **C**: burn churn x3; th d0 +0.0140; hinata d0 **+0.0370**, d3 +0.0267, d5 +0.0267; dragonstorm
  d0 -0.0340, d3 -0.0066; mirrorwing d0 +0.0020.
* **D2**: as C except hinata d0 **+0.0090**, d3 **+0.0133**, d5 +0.0267.
* **E**: burn churn x3; th **PASS** (Treasure Hunt is mv 2, so no longer promoted); hinata d0
  **+0.0050**, d3 +0.0133, d5 +0.0267; dragonstorm d0 -0.0340, d3 -0.0066; mirrorwing d0
  **-0.0160** (was +0.0020 -- Expressive Iteration at mv 2 was costing, not paying).

### The discernment bar was wrong, and the measurement says where

`IdealOrderCantripMaxMv` was set to 2 by the reasoning that Ponder/Preordain (1) and
Expressive Iteration / Light Up the Stage / Treasure Hunt (2) all leave the turn's mana
"essentially intact". Measured, that is false for the mv-2 half: dropping the bar to **1** removes
th's +0.0140 entirely and flips mirrorwing d0 from +0.0020 to **-0.0160**, while costing nothing.
A 2-mana cantrip is not a free look on these decks -- on a turn with 2-3 lands it IS the turn, which
is the same argument that already excluded Magma Opus, just applied at the right threshold.

Five things this settles:

1. **The ORDER is worth having on its own.** Nearly all of arm B's win is dragonstorm d0 (-0.0340)
   -- a ritual deck whose opaque sets were being cast in plan order. That is step 3 paying off with
   principle 1 switched off. The promotion is only worth having once BOTH corrections are in
   (the range, and the mv-1 bar): arm C +0.00151 -> arm E -0.00265.
2. **The range works, and it is the single largest thing making the promotion viable.** Isolated at
   the mv-1 bar (F vs E), the ladder is worth **-0.0017 per game** -- it recovers 83% of what the
   promotion breaks on hinata d0 (+0.0300 -> +0.0050) and more than doubles the arm's total. And
   arm D vs D2 shows the same lever is worth NOTHING when its projection declines producers: a
   lever that early-outs looks exactly like a lever that fires and finds nothing, and only a probe
   that counts ENTRIES tells them apart.
3. **The best arm's margin over ordering-alone is THIN, and it is two decks pulling opposite ways.**
   E beats B by only -0.00019 per game: mirrorwing d0 -16.0 game-turns against hinata +13.0. The
   confident result on these seeds is `MTG_ORDER_OPAQUE` (-0.00246, almost all of it dragonstorm
   d0); the promotion on top is at best a wash, and only after both corrections. Held-out seeds
   should decide whether E's margin survives, and adoption is the USER's call either way.
4. **The hinata regression is NOT truncation.** Same case (d3, 150 games, seed 1001) at 100x the
   budget: base 5.7400 / 5.7067 / 5.7000 and promotion 5.7667 / 5.7333 / 5.7267 at budget
   10 / 100 / 1000 -- the gap is +0.0267, +0.0266, +0.0267. Constant across two orders of
   magnitude, so the search is not being starved; casting the cantrip first is simply worse play
   there.

### HELD-OUT (regression mode, seeds 2002/3003, 26,300 games) -- the promotion is a PER-DECK call

| arm | per-game | dragonstorm | mirrorwing | hinata | burn |
|---|---|---|---|---|---|
| B (`ORDER_OPAQUE`) | -0.00236 | -57.04 | - | -6.00 | +1.00 |
| E (+ ideal + range + mv 1, GLOBAL promotion) | -0.00232 | -57.04 | **-20.00** | **+15.00** | +1.00 |
| **G (+ range, PER-DECK promotion)** | **-0.00312** | -57.04 | **-20.00** | **-6.00** | +1.00 |

(game-turn totals, negative = better.)

**Arm G is the answer the split predicts, and it lands on both seed sets** -- Mirrorwing's -20.00
and Hinata's -6.00 at the same time, because each deck now gets the order it measures better with:

| arm | smoke (15,875 games) | regression (26,300 games) |
|---|---|---|
| B | -0.00246 | -0.00236 |
| E | -0.00265 | -0.00232 |
| **G** | **-0.00346** | **-0.00312** |

G is additive by construction: no deck is worse than under B, and the two decks that move are the
two the promotion was measured on. It is also the arm with no seed-set disagreement -- unlike E,
whose margin over B changed sign.

**E's margin over B flips sign between the seed sets** -- -0.00019 on smoke, +0.00004 here. It is
not there. But the per-deck split underneath it replicates precisely:

* **`ORDER_OPAQUE` replicates and is the whole win**: dragonstorm d0 -0.0340 (smoke) / -0.0570
  (regression), identical in both arms because the promotion does not touch that deck.
* **Mirrorwing consistently GAINS from the promotion**: -16.0 game-turns (smoke) / -20.0
  (regression), and on regression it is better on 4 of its 5 keys.
* **Hinata consistently LOSES**: +13.0 (smoke) / +15.0 (regression).

Two decks pulling opposite ways is not a lever to tune at the root, it is a per-deck judgement --
so the promotion moved to `DecisionProvider::PromoteCantripsInCastOrder()` (default false),
overridden by MirrorwingProvider behind `MTG_MW_CANTRIP_ORDER`. That is the skill's standing rule
("adopt in the archetype provider, never the root") arrived at from the measurement rather than
applied by habit. Adoption is still the USER's call.

The mv bar moved 2 -> 1 as a *default* on the same evidence: it costs nothing anywhere, removes
th's +0.0140, and is what makes the Mirrorwing win a win (at mv 2 the promotion also catches Fists
of Flame, the deck's payoff, and mirrorwing d0 goes -0.0160 -> +0.0020). Inert while every
promotion route is off.

### Why principle 1 under-delivers here: the engine already owns half of it

"Draw before playing land or rituals" is two rules, and a cast-order rank can only express the
second one -- the land drop is not a cast and is not in `CastOrderRank`'s domain at all:

* **At depth > 0 the land drop is FOLDED INTO THE SEARCH** (`fold_land` in AIEngine::TakeTurn, and
  `MTG_MAIN2_DROP` extends it to the second main). The search already weighs "land before or after
  the cantrip", so promoting the cantrip ahead of the *casts* adds nothing there and only costs.
* **At depth 0 the rule exists as ONE hand-written deck special case** -- "TH before land drop":
  when Treasure Hunt is castable and no Reliquary Tower / Land's Edge is out, defer the land drop
  so a land TH draws can be played. That is principle 1, for one card, written by hand.

So the untested half of principle 1 is the *generalisation of that special case* to any castable
promoted cantrip at depth 0 -- a land-drop deferral, not a cast-order rank. That is the candidate
this measurement points at, and it is a different mechanism from the one measured above.

## Build order

Each step is independently measurable, default-off, and byte-identical off.

1. **DONE (e9e0c8d, 6d4761b)** -- **rank the three principles** in the generic `CastOrderRank`:
   draws ahead of the land drop and rituals; cost reducers ahead of their reducible casts; enablers
   ahead of payoffs (generalising Anti-Lifegain's rank 0). Gate: `MTG_IDEAL_ORDER`. Only the draw
   tier is wired; principles 2 and 3 were already ranks (16 and 0/19).
2. **DONE (2ee99b9)** -- **give affordability-affecting spells a range** and implement the fallback
   ladder, so an unpayable ideal order steps down instead of failing. Gate: `MTG_ORDER_RANGE`. This
   is what lets step 1 be aggressive without stranding mana.
3. **DONE (2ee99b9)** -- **retire `OrderingOpaque`'s ORDER bail-out** (not the breakpoint machinery
   -- see above). Gate: `MTG_ORDER_OPAQUE`. Then measure the breakpoint fan's cost with the order no
   longer ambiguous. Hoped-for: rollout calls fall back toward the 1,625 baseline, and the cantrip
   class becomes affordable at hinata's budget of 10 -- which is the 5->8 family
   (`classify-stack-adoptable-subset.md`). NOT yet measured.
4. Suite train, then held-out overnight, before any adoption claim. Adoption is the USER's call.

## Open questions to settle by measurement, not assumption

* **Where does the land drop sit relative to a ritual?** Principle 1 orders the draw before both,
  but not those two against each other. The deferred-drop work (`MTG_MAIN2_DROP`, 2cd3f51) says a
  land wants to be as late as the information allows; a ritual wants to be as early as payment
  allows. They may not conflict in practice -- measure before adding a rule.
* **Does "enabler before payoff" need a dependency map, or is a param-derived rank enough?**
  `docs/design/card-dependency-map.md` already carries the enabler/payoff pairs the arc has found.
* **Range granularity.** The minimum viable range is two rungs (ideal, cost-efficient). Whether
  intermediate rungs ever pay is an empirical question and should not be built until one is shown.

## USER REVIEW: Anti-Lifegain (2026-08-18)

The first deck reviewed under the review rule. Five instructions, all implemented behind
default-off levers, with what each one measured.

| # | USER instruction | implemented as | measured |
|---|---|---|---|
| 1 | "m1 cards should be first in the order if there is no reason to do it otherwise" | `CastOrderKey` folds the natural phase in UNDER the rank (`MTG_ORDER_M1_FIRST`) -- Ignoble Hierarch (m1) now precedes Birds of Paradise (m2), both rank-10 dorks | neutral: +1.0 game-turns over 15,875 (7 decks change play, all d3/d5 pure churn) |
| 2 | "Swords should go after Invigorate (because of the invigorate swords play)" | rank 21, DERIVED from `controller_lifegain_equals_power` -- a spell paying life equal to POWER is a payoff of every spell that raises power | inert, see below |
| 3 | "Skyshroud cutter can go lower in the cast order, to fit in with m2" | rank 24, derived from "vanilla creature whose whole function is its alt-cost gift" | folded into the antilife delta |
| 4 | "Reverent Silence should be cast last, so it should be m2" | `AntiLifegainProvider::MainPhaseOverride` -> Main2 | records the intent; changes nothing in play (see the note on the filter) |
| 5 | "we are allowed to recheck Tainted Remedy + Reverent Silence at the end of the list" | `ApplyEnablerWipeRecheck` (`MTG_ORDER_RECHECK`) | the ordering WORKS; the delivery does not -- see below |

### The m1/m2 column is advisory today

`MainPhaseFilterActive` is opt-in per provider (`ClassifiesMainPhases`) and **no provider overrides
it**, so the pre-combat Main2 filter never runs. Instruction 4 is therefore recorded but inert, and
instruction 1 is the only thing that gives the phase column any effect on play at all.

### Instruction 2 is correct, live, and MEASURED -- two of my claims about it were wrong

**Correction (USER, 2026-08-18: "That's wrong on Swords. We do cast it on opponent's creatures"),
and a second correction on top of it (USER: "we don't play Orchard. The opponent's creatures are
only spawns that happen in specific matches").**

I first wrote that the play was unreachable because the goldfish opponent has no creatures, then
"corrected" that by quoting an Orchard-Spirit example out of
`docs/design/antilifegain-swords-targeting.md` -- which describes a different context. This deck's
list has **no Forbidden Orchard**, and none of the engine's three opponent-creature sources
(`taps_spawn_opp_token`, `etb_opp_creates_tokens`, `cumulative_upkeep_opp_token`) is in it.

The real source is `GoldFishRunner::PopulateOpponentSpawns`: a **10-game repeating cycle of passive
opponent boards keyed by game index**. Slots 0 and 1 are pure goldfish; the other eight schedule
creatures at set turns (a weenie swarm, a midrange 2/2 board, a lone 4/4 on T3, a 6/6 wall + 1/1,
...). They never attack or block -- they exist to be targets. So **opponent creatures are present in
8 of every 10 games**, on a fixed schedule.

Verified in play, not inferred: 60 logged antilife games contain 115 Swords mentions and real casts
such as `targets: [{cardName: "2/2 Creature", who: "opponent"}]` in game 13 (index 3 -> the midrange
pattern) and game 29 (index 9 -> a 2/2 on T3), with **zero** Spirit tokens anywhere. That spawn
cycle is also why the rank A/B below moves play at all -- had the target set been empty, Swords'
rank could not have changed a single game.

The play is not merely reachable, it is IMPLEMENTED, and by a route worth knowing:
`TryPumpThenSwordsRedirect` (SpellEffects.h) pulls a free alt-cost Invigorate out of HAND during
Swords' RESOLUTION and puts the +4/+4 on the Swords target, for `power + pump + alt_lifegain` of
opponent life loss. That redirect is the only way Invigorate ever pumps an OPPONENT creature -- cast
normally it is `target_own_creature`.

**My second claim was also wrong, and the measurement is what says so.** I reasoned that since the
redirect needs Invigorate still in hand, ranking Swords AFTER the pumps would defeat it, and that
the instruction should be inverted. A/B on antilife smoke (`MTG_AL_SWORDS_RANK`):

| Swords rank | antilife d0 | verdict |
|---|---|---|
| **21 -- after the pumps (as instructed)** | **4.9030 = GT**, churn at d0/d3/d5 | neutral, safe |
| 17 -- before the pumps (my inversion) | 4.9160 (**+0.0130**) | worse |

The instruction stands as given. The inversion loses because the safe alt payloads are auto-fired
AFTER the casts rather than enumerated into the order (`FireSafeAltPayloads`), so ranking Swords
late never consumes the Invigorate the redirect wants -- while ranking it early just casts the
removal ahead of the deck's actual business. Reasoning about the redirect was right; reasoning
about which rank served it was not, and only the A/B separated them.

### Instruction 5: the line is now EXPRESSIBLE, and it still is not worth delivering

Reverent Silence's alt cost gifts 6 life -- flipped to 6 damage by a live Remedy -- and then its
resolution destroys that Remedy. The gift is **paid at cast** (`AIEngine::CastSpellFromHand`:
`OpponentGainsLife` fires before resolution), so the Remedy backs the wipe and only then dies. A
second Silence needs a second Remedy in between, which is the USER's alternation.

`ShouldEmitRiskyAltPayload` condition (a) had rejected exactly this, in writing: *"the 'Reverent +
2nd Remedy + Reverent' rebuild needs cross-turn sequencing the engine does not model"*, because
*"enabler-first casts it before Reverent, so it is wiped too"*. **That reason was an ORDERING limit,
and it is now false** -- `ApplyEnablerWipeRecheck` holds the replacement back and casts it after the
wipe. Measured firing: 47 times in 250 games, including 2- and 3-wipe alternations.

But the only route that reaches the line also hands it to the GREEDY consumer, and that is the
failure the gate was built for:

* `MTG_ORDER_RECHECK` + opening the gate on "a replacement enabler is in hand": antilife d0
  **4.9030 -> 4.9550 (+0.0520)**, searched d3 **4.1880 -> 4.1840 (-0.0040)**, d5 churn.
* `MTG_ORDER_RECHECK` with the gate left shut: **completely inert, 36/36 PASS.**

So the ordering machinery is right and the delivery is wrong. The searched depths WANT the line; the
greedy policy bricks the combo for an immediate 6, exactly as the old comment predicted, and it
outweighs the gain 52:1 by game-count. The gate change is reverted and the measurement recorded in
its place. **The clean route is search-only emission** -- `CollectActions`' `search_risky_live` path
already bypasses the greedy gate when a Remedy is live, but does not enumerate the
replacement-Remedy-then-second-Silence shape; teaching it to is the next step, and it leaves the
greedy policy untouched by construction.

### Instruction from the same review, not yet acted on

"Land drops should be ordered for decks. In this case, it should be put at the start, since there is
no draw." The land drop is now a ROW in `--cast-order-report` (it had none, because it is not a cast
and has no `CastOrderRank`), annotated with how it is actually decided: before every cast at depth 0,
folded into the search at depth > 0. For Anti-Lifegain "at the start" is what already happens. Making
the position a per-deck reviewable *decision* is the open half of principle 1 (see above).

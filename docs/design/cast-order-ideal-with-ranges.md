# Cast order: an ideal order with affordability RANGES, searched only to stay castable

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

## Build order

Each step is independently measurable, default-off, and byte-identical off.

1. **Rank the three principles** in the generic `CastOrderRank`: draws ahead of the land drop and
   rituals; cost reducers ahead of their reducible casts; enablers ahead of payoffs (generalising
   Anti-Lifegain's rank 0). Gate: `MTG_IDEAL_ORDER`. Measure alone -- this alone should move play
   on cantrip decks with the breakpoint class OFF, and it is the cheap half.
2. **Give affordability-affecting spells a range** and implement the fallback ladder at the payment
   site (`ManaPayment`), so an unpayable ideal order steps down instead of failing. Gate:
   `MTG_ORDER_RANGE`. This is what lets step 1 be aggressive without stranding mana.
3. **Retire `OrderingOpaque` for the draw class** once 1+2 hold, and measure the breakpoint fan's
   cost with the order no longer ambiguous. Expected: rollout calls fall back toward the 1,625
   baseline, and the cantrip class becomes affordable at hinata's budget of 10 -- which is the
   5->8 family (`classify-stack-adoptable-subset.md`).
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

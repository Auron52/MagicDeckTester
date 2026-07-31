# Cantrip-first: a dominance collapse for the plain-cantrip breakpoint class

2026-07-31. Direction from the user, in response to the measurement that the plain-cantrip
breakpoint class (`MTG_BP_SITES` bit 3) is the sole held-out regressor on Hinata:

> "In searched do not put any other spells before the cantrip in the current turn fragment, with
> perhaps an exception for mana-generating acceleration like Reality Spasm or cost reduction like
> Hinata so that Soulfire Eruption would be unaffected. (other mana generators like lands could also
> be played) We would also prioritize branches that play it first, with nothing cast before. So, in
> other words, if you cast a cantrip in this segment you need to obey certain rules that limit the
> branching factor."

## Why this is a dominance prune, not a heuristic

Drawing a card earlier is **strictly more information** for every decision that follows it in the
same turn. So for any two casts A (non-cantrip) and C (cantrip) in one fragment, `C then A`
dominates `A then C` — the line reaches the same board with strictly more known. The *only* reason
`A then C` can be necessary is that A pays for C:

- **mana acceleration** — a ritual / mana rock / sac-for-mana whose float funds the cantrip
  (Reality Spasm),
- **cost reduction** — a reducer that makes the cantrip (or a later spell) affordable (Hinata),
- **lands** — not a cast at all (the land drop is a separate plan field), so a land needed to PAY
  for the cantrip may precede it.

Note Soulfire Eruption is not itself exempt; it is *unaffected* because the thing that legitimately
precedes it is Hinata's reduction, which is.

### It is NOT pure dominance: cast-triggered payoffs (user correction, same session)

> "To be fair I can think of some cases where this is not pure dominance, but those are 'I benefit
> from cantrips being cast' type of effects like Guttersnipe, Vivi etc. We can have a list of cards
> that fits the bill for each deck as needed."

Correct, and it adds a THIRD exemption category of a different kind. The first two are about
**affordability**; this one is about **value**: a permanent that triggers on each spell cast
(Guttersnipe, Vivi Ornitier, prowess, Aria of Flame) must be deployed BEFORE the cantrip, because
then the cantrip's own cast is worth damage/pump on top of the card. Deploying it after wastes a
trigger. Those cards are neither accelerants nor reducers, so the affordability-only rule would
wrongly forbid the correct line.

So the exemption is "anything that makes the cantrip **cheaper or better**":

| category | reason | examples |
|----------|--------|----------|
| mana acceleration | affordability | Reality Spasm, rituals, rocks, Lotus sac |
| cost reduction | affordability | Hinata |
| funding land | affordability | the drop that pays for it |
| **cast-triggered payoff** | **value** | **Guttersnipe, Vivi, prowess, Aria of Flame** |

**Derive the fourth category from a card PARAM; do not hand-list it.** (User: "That would be much
better than a list if we checked for a trigger.") A per-deck name list is the failure mode: an
unlisted payoff card silently loses a real line, and a quality prune that depends on someone
remembering a card is exactly the "surprise heuristic" this effort is removing. A param cannot be
forgotten by a rule author — implementing the card is what registers it.

**What exists today, checked 2026-07-31 — the param is NOT yet there.** `CardParams` has
`on_cast_trigger_max_mv` / `on_cast_trigger_damage`, but its documented meaning is *Eidolon of the
Great Revel*: "when the controller casts a spell with MV <= max_mv, deal damage **to that player**".
That is a symmetric PUNISHER aimed at the caster — the opposite polarity to a Guttersnipe payoff.
`CollectTriggerSources` (`TurnSolver.cpp`) scans the battlefield for exactly that param and nothing
else, so it does not currently identify payoff permanents. No suite deck has a Guttersnipe/Vivi, so
the category is presently EMPTY rather than mis-modelled.

What the rule needs, therefore:

1. **A payoff-polarity param**, added when the first such card is implemented — either a target
   field on the existing pair (`on_cast_trigger_target` = self | opponent) or a separate
   `on_cast_payoff_*`. Prefer extending the existing pair so one mechanism covers both polarities.
2. **The exemption predicate reads the ACTION, not the board.** The board scan
   (`CollectTriggerSources`) answers "what already triggers"; this rule needs "is the card I am
   about to cast a payoff I should deploy first", i.e. a check on `a.def->params` using the `def`
   pointer `CollectActions` already caches on every `Action`. O(1) per action, no lookup.
3. **Honour the MV gate.** These triggers fire only on spells with MV <= max_mv. If the cantrip's MV
   does not qualify, deploying the payoff first buys nothing and the collapse stays safe — a
   precision win that is checkable rather than assumed.

A card whose "benefits from casts" ability has no param at all is simply an unimplemented card,
which is a `cards.json` gap the coverage stage already catches — not a hole in this rule.

That also converts the rule from an assumed prune into a **checkable precondition**, which is
better than either:

> If the board has NO cast-trigger source and the subset has no affordability dependency, then
> cantrip-first is provably dominant and the collapse is unconditionally safe. Otherwise fall back
> to full enumeration for that node.

Most decks and most turns hit the safe path (no trigger sources at all), so the collapse still buys
the branching reduction where it matters, without ever being a guess. Where it does not hold, the
search simply keeps its current freedom. `MTG_UNPRUNED` (`UnprunedGate::CantripFirst`) still gates
the whole thing for the standing pruned-vs-unpruned A/B.

### The land drop should also come after (user, same session)

> "In fact, we could prioritize branches that don't even play a land before, since playing it after
> seeing what you draw is often best."

Same dominance argument, one step further: the land drop is itself a CHOICE, and after the cantrip
resolves you may be holding a land you would rather play (or the draw changes which land you want,
or whether to play one at all). So a land that is NOT needed to fund the cantrip should be deferred
into the post-breakpoint continuation rather than played ahead of it.

Two things make this cheap to do:

- The continuation is **already land-folded** — `EnumerateBreakpointPlans` returns plans carrying
  `land_to_play` / `fetch_target` / `land_face`, and `bp_play_searched_land` plays it. So deferring
  the drop hands it to a SEARCHED decision, not a greedy one.
- It **removes a special case**. `AIEngine::TakeTurn` currently hardcodes exactly this rule for one
  card: "when Treasure Hunt is castable and no enabler is in play, defer the land drop to the second
  pass so a land drawn by TH can be used." That is the cantrip-first land rule, written for a single
  template. Generalising it should delete that block rather than add to it — and the existing
  `PreferHoldLandDrop` / `HoldDeferredDropForFurtherDig` provider hooks are the natural home.

Caveat to measure, not assume: deferring the drop costs a turn of mana if the continuation then
declines to play one, and `MTG_LEGACY_2ND_MAIN_LAND` history shows land-timing changes have bitten
lockstep before (gi=141). The deferral must be a searched alternative, never an unconditional rule.

This puts the rule in the same family as the collapses already shipped in `TurnSolver.cpp` —
`NonPrefixAccelViolated`, `IndependentAccelPrefixViolated`, `SpliceCollapseViolated` — which reject
orderings that are provably equivalent-or-dominated rather than guessing an answer.

## The second, larger reason: WHERE the breakpoint lands

This is not only about branching factor. Measured 2026-07-31 (see
`post-breakpoint-search.md` and commit `52d7faa`), the cantrip class costs Hinata **+0.0392** avg
win turn on held-out seeds, and — critically — deferring it out of wave 0 (`MTG_BP_W0_SITES=0x17`)
made it **worse still (+0.0666)**. That rules out simple budget dilution: if the continuations were
merely unaffordable, spending less on them would help. They are being **mis-ranked**.

Cantrip-first explains the mis-ranking mechanically. The breakpoint fires when the cantrip
RESOLVES, mid-`ApplyPlanDirect`, in cast order. So:

- **cantrip last** → the breakpoint opens at the *end* of the turn. The continuation has almost
  nothing left to decide, so the W variants (and every deferred wave rank) are near-duplicates that
  consume nodes and dilute the ranking without being able to differ.
- **cantrip first** → the continuation IS the rest of the turn, re-solved with the drawn card in
  hand. That is the entire value of searching the class.

So the rule should raise quality and cut cost together, rather than trading them.

## Implementation sketch

Two halves, because a plan's cast order is canonical unless ordering search is on
(`WantsCastOrderingSearch`, Dragonstorm-only by default):

1. **Ordering / priority (the half that matters for Hinata).** `CastOrderRank` is already a
   provider hook. Rank cantrips immediately after accelerants/reducers and ahead of everything
   else, so the canonical order puts the cantrip first and the breakpoint lands early. This alone
   delivers "prioritize branches that play it first".
2. **Pruning (bites when ordering IS searched).** A subset/ordering guard rejecting any ordering
   where a non-exempt cast precedes a cantrip — same shape and placement as
   `NonPrefixAccelViolated`, evaluated in `EnumeratePlanPositions` / `Solve::consider`.

**Both must be provider-owned.** "Which cards are accelerants / reducers" is archetype knowledge,
and the engine's generic paths may not contain it (`mtg-ai.md`: heuristics live in deck/archetype
files, never in the main paths). Expose it as a provider predicate with a `MTG_UNPRUNED` gate
(`UnprunedGate::CantripFirst`) so the standing pruned-vs-unpruned A/B can open it, exactly like
`AccelPrefix` and `SpliceCollapse`.

### Open question worth deciding before building

A stronger variant is available and may be much better: since the breakpoint **re-solves the rest of
the turn anyway**, a plan of the form "exempt accelerants → cantrip → *anything*" has its tail
largely superseded by the re-solve. If so, the whole family of such plans collapses to ONE base
plan, which is a far bigger reduction than reordering alone. Worth measuring, but it is a genuine
behaviour change (the base plan's post-cantrip casts are currently executed, with the re-solve only
ADDING casts from revealed cards), so it should be a separate arm, not folded into the ordering fix.

## Validation target

The benchmark this must beat is the current default (`MTG_BP_SITES=0x17`, class off), whose Hinata
held-out delta is **0.0000**. Run `MTG_BP_SITES=31` **with** the rule against it on the overnight
seeds; the class earns its place only if it lands at or below 0.0000 there. Also re-check TH and
Dragonstorm, which currently gain from nesting and must not lose it (th −0.0040, dragonstorm
−0.0020 held-out).

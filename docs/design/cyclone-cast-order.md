# Unpredictable Cyclone — candidate cast order (FOR USER REVIEW, not adopted)

Status 2026-10-29: a DRAFT for the USER. Nothing is built; the deck plays the generic order below until the
USER rules. Cast order, range and main-phase split are USER-owned (`cast-order-ideal-with-ranges.md`, the
review rule).

## What plays today (generic, `mtg <deck> --cast-order-report`)

`CycloneProvider` does not override `CastOrderRank`, so the generic tiers apply:

| rank | cards |
|---|---|
| land | land drop first (depth 0; searched inside the window at depth > 0) |
| 10 creature | Curator, Street Wraith, Ulamog CH, Ulamog IG, Emrakul |
| 15 ritual | Seething Song |
| 20 other | Brainstorm, Ponder, Remand, Brainsurge, Long-Term Plans, Toxic Deluge, Unpredictable Cyclone |
| last | cycling (structural: after every cast of its window) |

Two facts about where the rank is actually read:

1. **Turns with a draw spell ignore it.** `OrderingOpaque` (ManaPayment.cpp) skips the rank sort for any set
   holding a card with `draw > 0`. That covers Brainstorm, Ponder, Brainsurge and Remand, and those turns keep the
   search's plan/breakpoint order. So today the rank orders only Seething Song, Cyclone, Long-Term Plans,
   Curator and the Eldrazi. A provider can opt the draw turns in (`OrderOpaqueCastsByRank`, as
   BreachingDragonstorm does).
2. **The fallback range is off.** The range ladder (`MTG_ORDER_RANGE`, default OFF) moves a spell toward its
   cost-efficient end when the ideal order cannot be paid.

Defects in the generic order for this deck:

- **A hard-cast Eldrazi ranks AHEAD of the Seething Song that pays for it** (creature 10 < ritual 15). An
  8-land Song into Ulamog CH would cast the Ulamog first. The generic code's own comment calls this the
  stranding shape. It is rare (late game, hard cast), and I have not checked whether it occurs in play.
- **Curator (hard cast) ranks ahead of Song and Cyclone** for a prowess reason that does not apply here.
- **Cyclone ties with Long-Term Plans and the cantrips at 20**, so nothing deck-specific places it.

## The candidate (lower = earlier; all main 1)

| # | card | why here | range (fallback when the ideal order can't be paid) |
|---|---|---|---|
| 1 | **Long-Term Plans** | Its shuffle erases any top a cantrip just arranged, so it goes BEFORE them. The target lands 3rd from the top, where Ponder sees it and Brainstorm (draw 3) draws it, or a put-back re-tops it. Its targets are already ruled: a missing Cyclone, or Emrakul to set up. | **to after the put-back cantrips**, for the shuffle line ("Brainstorm junk back, then shuffle it away"). This is the only case where LTP wants to be later. |
| 2 | **Ponder** | Look before you put back. Ponder first means Brainstorm draws three cards we have SEEN (or a fresh shuffle). Ponder after a Brainstorm that topped an Eldrazi would DRAW the Eldrazi, which is what we must not do. | **to after Brainstorm**: Ponder's own shuffle as the shuffle line (Brainstorm two junk cards back, Ponder sees them, shuffles). |
| 3 | **Brainstorm** | The cheap put-back: draws first (principle 1), before the land, the ritual and Cyclone, so those are chosen knowing what it found. Its put-back sets the walk (your go-off rules). | to the cost-efficient end (after Song/Cyclone) only when its {U} cannot otherwise be paid. |
| 4 | **Brainsurge** | The 3-mana put-back. After Brainstorm so the cheap one spends {U} first and the 3 mana stays uncommitted until we know whether Song/LTP/Cyclone wants it. **Judgement call (A below).** | to after Song when Song's red float pays its {2}. |
| 5 | **Remand + a 1-drop** (the {1}{U}{U} "cycle") | A draw, but the most expensive per card, so last among the draws: the real cantrips may find what makes the 3 mana better spent on the go-off. (Remand on a Cyclone hit is part of the cycle step, not of this order.) | to after Cyclone (its cost-efficient end, as the report already marks it). |
| — | **land drop** | After the draws (principle 1). **Fetch nuance:** a Scalding Tarn (or a Myriad Landscape activation) SHUFFLES, so it must be cracked BEFORE a put-back that sets up the walk, and AFTER a put-back that throws junk back (your shuffle line). | Before the draws when a draw needs the land's mana. Land is searched inside the window anyway; this governs the depth-0 playout. |
| 6 | **Seething Song** | Ritual: after the draws (principle 1), before everything it pays for (Cyclone, a hard-cast Eldrazi). Its float is RED only, so it can pay generic and red, never Brainstorm/Ponder/Curator's {U}. | **to before Brainsurge / LTP** when its float pays their {2}. |
| 7 | **Unpredictable Cyclone** | The engine: after Song (funded by it), after the draws (they can find the Song, the land or the cycler, and the put-back is chosen knowing the go-off), before every cycle (already structural) and before the payoffs. | — |
| 8 | **Curator of Mysteries** (hard cast) | After the draws: a Brainstorm may want to keep it as a cycler or put it back, so casting it first throws that option away. After Cyclone: no interaction between them, and Cyclone gets the red/generic first. On the battlefield it scries on each later cycle (structural). | — |
| 9 | **Street Wraith** (hard cast) | {3}{B}{B}: essentially never; free casts come from walks. Listed for totality. | — |
| 10 | **Ulamog, the Ceaseless Hunger** | Payoffs last (enablers before payoffs): after the Song that funds them. CH's cast trigger exiles two permanents, and with no opponent spawns it is FORCED onto ours, so everything else should be down and used first. | — |
| 11 | **Ulamog, the Infinite Gyre** | Same reason. | — |
| 12 | **Emrakul, the Aeons Torn** | Same. Its extra turn does not care about position. | — |
| never | **Toxic Deluge** | Goldfish-inert (your ruling); never cast. | — |
| last | **cycling** | Structural: after every cast of its window. Which cycler is searched. | — |

Tie rule: by name (`CastOrderTotalByName`), so the order is total.

## Interactions considered and found NOT to need an order

- **Cyclone vs cantrips:** Cyclone replaces only a CYCLING draw, so Brainstorm/Ponder/Brainsurge draw normally
  on either side of it. The question is information only, so the draws go first.
- **Curator vs Cyclone:** neither triggers on a cast.
- **Two put-back spells in one turn:** the engine pins (searches) only ONE put-back per plan. The other takes
  the provider default whatever the order (a known limitation, Brainsurge's card note).
- **A second Cyclone:** surplus (one replacement applies). The search decides whether to cast it; no rank needed.
- **Main phase:** single main for everything, as now. A free-cast or hard-cast Eldrazi is summoning-sick in
  either main, and nothing gains from combat first.

## Judgement calls for the USER

A. **Brainstorm before Brainsurge, or the reverse?** Both see five new cards in total either way. I put the
   1-mana one first to keep the 3 mana uncommitted longer. This is close to a coin flip.
B. **Ponder before Brainstorm by default**, with Brainstorm → Ponder as the shuffle-line fallback. Agree?
C. **Long-Term Plans first**, before the cantrips (its shuffle would erase their setup), with the shuffle line
   as its fallback. Agree?
D. **Should the order also govern draw turns?** Today any turn holding a draw spell skips the rank, and the
   search orders it. Opting in (`OrderOpaqueCastsByRank`) is what makes "draw first" real, and it removes order
   fan-out. But it is a RESTRICTION, so it gets its own lever, a paired A/B and the known-top blind
   classification. Proposed staging: (1) the ranks alone (Song/Cyclone/LTP/Curator/Eldrazi turns, plus the
   playout); (2) the draw-turn opt-in with the range ladder on, measured separately.
E. **Blind cycling as a draw.** Cycling is structurally LAST today. A Street Wraith cycled with no Cyclone out is
   a free draw, and principle 1 would want it before the land and the casts. But it spends a combo piece (you want
   1-3 cyclers kept). Leave it last?

## USER review 2026-10-29

*"The order looks reasonable, though we should also specify the cycle order. Maybe from cheapest to most
expensive? Arguably Cyclone cycling should go earlier, but I suppose it doesn't matter all that much because of
breakpoints and re-solves."* And: *"Unless we decide that we need to use condemnation for cost reasons, but I'm
not sure this deck would respond well to that."*

So the cast order above (A, B, C at the drafted defaults) goes to build and measurement. D (the draw-turn
opt-in) is measured as its own arm. E is replaced by the cycle order below.

## Cycle order (draft 2, for the USER)

Today: the SEARCHED cycle sources are ordered creatures, other nonlands, lands, cheaper first. The CYCLER rule
(when ON) puts Curator before Wraith. The PLAYOUT (`CycloneProvider::SelectDigSource`, where most of the units
go) picks the CHEAPEST payable creature cycler, i.e. Street Wraith first.

**Cheapest-first conflicts with the USER's own 2026-10-28 cycler rule:** *"You want to use whichever you have
spare mana for ... I like to keep Street Wraith for Cyclone when that is an option, but when cyclone is out the
order only matters in terms of mana utilization."* The CYCLER rule builds exactly that: with Cyclone out, Curator
first while a {U} is spare (it keeps the free Wraith for a turn when the mana is short), Wraith when it is not.
The playout's Wraith-first pick disagrees with it.

Proposed, with Cyclone OUT (creature cycles are walks):
1. Curator ({U}) while a {U} is spare after the turn's casts; otherwise skip to 2 (the USER rule).
2. Street Wraith (free).
3. Xander's Lounge ({3}): a land, so a PLAIN draw even with Cyclone out. AFTER the walks, because before them it
   would draw the Eldrazi set up on top.
4. A surplus Cyclone ({2}): with one out it walks to the next ENCHANTMENT (another Cyclone). It draws nothing, a
   whole-library whiff is a shuffle, and it bottoms a set-up Eldrazi on the way. Last.

With Cyclone NOT out (every cycle is a plain draw):
1. **A surplus Cyclone (2 in hand): cycle it BEFORE the other one is cast.** This is the case where "Cyclone
   cycling earlier" is a real interaction rather than a preference: after the first Cyclone resolves, cycling the
   second draws nothing (above).
2. Xander's Lounge, when it is not this turn's land drop.
3. Curator.
4. Street Wraith last: kept for the Cyclone (the USER rule) when one is in hand or at a known library position.

Measured against the USER's cheapest-first as a second arm.

**On condemnation:** agreed that it is a poor fit. Each walk can free-cast a Curator or bottom what it passed,
and the re-solve after every cycle is what uses that. Condemnation's earlier measurement (FiveColour,
`cast-order-ideal-with-ranges.md`) showed its savings were incomplete refutations. And the cost agent's census
puts under 2% of Cyclone's units in the search window, with 98% in the horizon playout, so a window-side cut buys
little anyway.

## If approved: build and measurement

`CycloneProvider::CastOrderRank` / `CastOrderTierName` / `CastOrderTotalByName` behind `MTG_CYCLONE_CAST_ORDER`
(HeuristicArm slot, so arms pool in one batch). Paired held-out A/B at d3 b10 and d5 b20 against the generic
order, with every slower game classified under the known-top blind. The result and a recommendation go to the
USER, who decides. Timing: a play change belongs BEFORE the mulligan-generation freeze, because the keep table
is fitted to the play.

# Porting engine-embedded heuristics to the provider interface

2026-07-31. Direction from the user:

> "I don't want any surprise greedy implementation or built-in heuristics ... literally every
> assumption is part of a provider."
> "The easiest way to move forward is to port most engine logic that acts heuristically to the
> providers so that we have no behaviour change to start."
> "Though we should discuss what the rules are that we are porting over of course. If they are bad
> rules we will want to change them after."

So the sequence per rule is: **(1) lift it into `DecisionProvider` byte-identically, (2) review the
rule itself, (3) widen it into a search branch.** This doc is step 2's agenda — what each rule
actually says, and where it looks wrong. It is deliberately separate from the *branching* work
(`searched-scry-disposition.md`, `searched-cleanup-discard.md`, `post-breakpoint-search.md`), which
is about coverage rather than about whether the default is any good.

## Already provider-owned (no port needed)

Verified 2026-07-31 — these were on the "not searched" list but are NOT hidden built-ins. Each is a
pure-virtual hook every provider must implement, so nothing falls back to an engine default:

| hook | decision |
|------|----------|
| `ShouldAttackWith` | combat: declare this creature as an attacker? |
| `ManaSourceRank` | mana-source tap order (lower = tap earlier) |
| `WantVialCharge` | add an Aether Vial charge counter this upkeep? |
| `ScryKeepOnTop` / `TopKeepRank` | scry/surveil: keep this card on top? |
| `KeepReorderTop` | Ponder-style: keep the top N or shuffle? |
| `DiscardLandsFirst` | is a land the preferred cleanup discard? |
| `CastOrderRank`, `SituationalCardRank`, `ArchetypeCardValue` | ordering / valuation |

They are single-answer hooks rather than candidate sets, which is the *branching* gap, not an
ownership gap.

## Rule 1 — cleanup discard (PORTED, byte-identical)

`SelectCleanupDiscardIndex` (`src/core/SpellEffects.h`) → `DecisionProvider::CleanupDiscardCandidates`.
The base implementation returns the historical single pick, so behaviour is unchanged.

**The rule as written:**

1. If `DiscardLandsFirst` (a Land's Edge-style outlet makes lands ammunition), shed **the first
   non-staged LAND in hand order**.
2. Otherwise the **highest-MV** non-staged card that is not a protected `required_piece`
   (protection scope per `DiscardProtectScope`; a redundant copy stays discardable).
3. Last resort, when every non-staged card is protected: **max-MV overall**, staged preferred.

**Review — two parts of this look wrong, independent of search coverage:**

- **1 is arbitrary.** "First land in hand order" is insertion order, so which land gets pitched is
  effectively random among the lands held. Lands are not fungible — Reliquary Tower, a fetch, a
  depletion land, and the only source of a colour are all different cards. This is a defect in the
  rule, not merely a missing branch: widening it to a search branch would fix it by accident, but
  the ranked default would still be junk.
- **2 uses MV as a proxy for "least useful".** MV correlates with castability, but in a ramp or
  storm deck the highest-MV card in hand is usually the *payoff* — precisely what you must not
  shed. `required_pieces` exists to patch exactly this, which is the tell: a per-deck protection
  list is a workaround for a bad global ranking. The measured `DiscardProtectScope` table in
  `DiscardPolicy.h` (Dragonstorm needs `LastInHand`, TH needs `All`) is the same tell.

Proposed replacement default, for discussion: rank by **"least likely to be cast before the game
ends"** — some combination of `ArchetypeCardValue` and affordability — rather than raw MV, and rank
lands by `ManaSourceRank` (already provider-owned) instead of hand order.

## Rule 2 — firebreathing activation count (NOT YET PORTED)

`ApplyFirebreathing` (`src/core/SpellEffects.h`), driven by `AIEngine::Firebreathe`. No provider
hook at all; autonomous play is greedy-max.

**The rule as written:** loop while mana remains, each iteration picking the activation with the
best **damage per mana value** (self-pump `firebreathing_power / cost.ManaValue()` vs team-pump
`power * matching attackers / cost.ManaValue()`), applying it, and repeating.

**Review:** for a goldfish this is *nearly* right — mana held past combat has no use for a deck
with no second main — but it has no notion of *whether the damage is needed*. It will dump the pool
into a pump when combat is already lethal, and it cannot express "hold {R} for the Scourge ping".
Greedy-max is a reasonable default; it should not be the only reachable answer. The human-play path
already asks for a count in `[0, max]`, so the enumeration exists — it is simply not offered to the
search.

### MEASURED 2026-07-31: greedy-max is DOMINANT in the current model, so there is nothing to search

The "no second main" clause above is the whole argument, and it was an assumption. It is now a
measurement. `ApplyFirebreathing` takes its `ManaPool` **by value** and never taps a real source, so
the pump is free unless something later in the turn wants that mana — i.e. unless a post-combat main
casts on a turn that pumped. `MTG_FB_TRACE=1` (diagnostic, no play change; `GameEngine::MainPhase`
compares `spells_cast_this_turn` across main 2 against `g_fb_activations_this_turn`) counts exactly
that:

| deck | pumping combats | main-2 casts after a pump |
|---|---|---|
| Dragonstorm | 88 | **0** |
| Dragons | 252 | **0** |

300 games each, seed 4004, d3. Both firebreathing cards (Scourge of Valkas, Lathliss) are in both
decks, so the sample is real, not vacuous.

With no competing use, every extra activation is weakly more face damage at zero cost, so
greedy-max **weakly dominates every smaller count** — the alternatives a search would enumerate are
dominated options, not rival answers. This is the one #6-class decision that does *not* want a
branch; the `FirebreatheActivations` hook stays as the escape hatch for a deck that ever does need
to hold mana (a real second main, an instant), and `MTG_FB_TRACE` is the standing check that says
when that day arrives. If it ever prints `DOUBLE-SPEND`, the pool-not-tapped shortcut has become a
modelling bug and *then* the count becomes a real decision.

## Rule 3 — scry/surveil disposition composition (PARTIALLY PROVIDER-OWNED)

`HeuristicTopDisposition` (`src/core/SpellEffects.h`) composes provider answers into a placement:
wanted cards (`ScryKeepOnTop`) first, ordered by `SituationalCardRank`, then the rest in look order;
for Reorder, `KeepReorderTop` decides keep-vs-shuffle wholesale.

**Review:** the *inputs* are provider-owned, the *composition* is engine-side. The composition is
mostly harmless (for Scry the non-kept cards are bottomed, so their relative order is unobservable),
so this is the weakest of the three cases. The real gap here is branching, not ownership — see
`searched-scry-disposition.md`.

## Status

| rule | ported | reviewed | branched |
|------|--------|----------|----------|
| cleanup discard | yes (byte-identical) | see above — **two objections** | no |
| firebreathing count | no | see above | no |
| scry disposition composition | n/a (inputs already hooks) | see above | no |

# Hinata: interleaving Ponder and Preordain by library state

**Status: DEFERRED, not started.** Raised by the USER on 2026-08-28 while reviewing the Hinata cast
order, explicitly after the current work: *"we can sort out Ponder/Preordain after."* This note
exists so the idea and its one non-obvious obstacle survive without being re-derived.

## The idea, in the USER's words

> "We might even do something trickier like choosing an interleaving order where you play Ponder
> first and Preordain if ponder has just been cast and we reordered rather than shuffled. i.e. You
> want Preordain more when the top 3 were kept and ponder is better when we don't know what the top
> 1-2 cards are."

So the two cantrips are not peers whose order is arbitrary; which one is better depends on what the
library's top looks like *right now*, and that changes as the turn's own cantrips resolve.

## Why the reasoning is right

Verified against `src/cards/data/cards.json` rather than recalled:

| card | cost | text |
|---|---|---|
| Ponder | `{U}` | Look at the top three cards of your library, then put them back in any order. You may shuffle. Draw a card. |
| Preordain | `{U}` | Scry 2, then draw a card. |

Ponder is modelled as `cast_reorder: 3` and is **all-or-nothing** -- keep all three on top in the
chosen order, or shuffle them away. Preordain is `cast_scry: 2`.

After a Ponder that KEPT and drew one card, the next two cards are ones the engine deliberately
chose. A Preordain cast into that position scries two cards it already arranged -- the scry is a
no-op -- and simply draws a card known to be wanted. A second Ponder into the same position spends
its look re-examining an arrangement it just made. Conversely, into an unarranged top, Ponder sees
three cards and can shuffle a bad top away, which Preordain cannot do at all.

## THE OBSTACLE: half of it is not expressible, and the naive form silently never fires

**"When we don't know what the top 1-2 cards are" has no referent in this engine.** The search is
clairvoyant -- it can see the library -- so there is no uncertainty state to condition a rank on. A
rule written as "we don't know the top" evaluates to "we always know", so it either never fires or
fires always, and either way it is not measuring what the ruling intends. This is the same class of
mistake as the recorded lesson that a lever must be verified to FIRE before a null result is read.

**The expressible form is the deliberate-arrangement fact, not the knowledge fact.** "Was the top of
the library deliberately arranged this turn (a keep, not a shuffle)?" is a property of the game, not
of an agent's information, and it captures exactly the case the ruling names.

## What it would take

* **One turn-scoped GameState field.** The keep-vs-shuffle call currently lives on the PLAN as
  `TurnSolver::Plan::ponder_keep` (`src/ai/TurnSolver.h`), the searched 2-way branch that
  `CollectActions` emits as two mutually exclusive variants, and it is carried to resolution through
  `AIEngine.cpp` so the executor's reorder matches the rollout's. Nothing persists "the top N were
  arranged" into `GameState` afterwards. A field in the shape of `Player::lands_played_this_turn` --
  say `top_arranged_this_turn` (how many cards of the top are known-arranged, decremented as they
  are drawn) -- is the missing piece.
* **No new mechanism beyond that.** `DecisionProvider::CastOrderRank(const GameState&, const
  CardDefinition&)` already takes the state, so a conditional rank is directly supported, and
  `CastOrderFallbackRanks` is the established way to express "preferred rank, with earlier rungs
  walked only while a condition holds" (Gold Rush's 15 -> 13 -> 6 ladder is the worked example).
* **Lockstep discipline.** Any new turn-scoped field must be set identically on the rollout and
  executor paths, or the committed line diverges from the searched one -- the recurring defect class
  documented in `rollout-executor-lockstep.md`.

## Prerequisite, and why this is sequenced last

Ponder and Preordain are currently PEERS at rank 10 in the Hinata order. `MTG_HINATA_PP_STRICT`
already exists and splits them (Ponder 10, Preordain 11) as a static order, and it has never been
measured with the dig class open. That static split is the cheap experiment and should be run first:
if a fixed order is already sufficient, the state-dependent version is unnecessary complexity; if
the fixed order is a wash in aggregate, that is itself evidence the right answer is situational.

Note the peer tie is not merely cosmetic under condemnation: `BpSlotIsAfterSite` exempts any
candidate ranked `>=` the site, so two cantrips on ONE rank are mutually un-condemnable. Splitting
them changes the condemnable set as well as the cast order, so the two effects must be attributed
separately -- the same "a lever spanning two effects is two levers" rule that has produced a wrong
verdict twice in this arc.

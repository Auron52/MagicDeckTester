# Direction: a provider PER DECK, sharing parts — not per archetype

**User direction, 2026-08-30.** Recorded here rather than acted on wholesale: it is a standing
direction for how providers should evolve, and the migration is incremental.

> "Realistically all decks should probably have their own, but perhaps share parts with similar
> decks. Unless they are literally of the same class — like different versions of mirrorwing. In
> those cases we want to share the provider."

## The rule

| relationship | provider |
|---|---|
| **Different VERSIONS of the same deck** (Mirrorwing v1-twinflame-anger vs v3-heroism-draught) | **SHARE** one provider outright |
| **Different decks that are merely similar** (Knights and slivers_vial; Goblins and Minotaur) | **OWN** provider each, **sharing parts** |

The test is deck identity, not archetype resemblance. Two lists that are the same deck with
different card choices are one deck for provider purposes — a screening arm is a declared
modification of its base, which is exactly what `MTG_PROVIDER_DECK` already encodes. Two genuinely
different decks that happen to share a mechanic are not.

## Why this is the right direction, in this codebase specifically

Provider routing detects archetype from CARD PARAMS, and several of those params are
archetype-neutral — `etb_self_creates_tokens`, `sac_creature_outlet`, `reduces_spell_subtype`
describe what a card DOES, not which deck it belongs to. Sharing a provider across merely-similar
decks therefore fails in a specific, repeated way: a deck inherits narrowing heuristics tuned for
someone else's deck, silently, because nothing errors.

That has now happened four times, every one found after the deck had already been measured:

| deck | trigger | inherited |
|---|---|---|
| Mirrorwing | Goblin Instigator's `etb_self_creates_tokens` | GoblinsProvider |
| StompySurprise | Hornet Queen's `etb_self_creates_tokens` | GoblinsProvider |
| Minotaur | Slaughter-Priest's `sac_creature_outlet` | GoblinsProvider |
| Dragons | Dragonspeaker Shaman's `reduces_spell_subtype` | GoblinsProvider |

The Minotaur case is the one that shows the stakes: the borrowed
`GoblinsProvider::DeferSacOutletPreCombat` deferred a sacrifice outlet to a second main phase the
deck does not have, so the outlet was not delayed but **deleted** from the search — a Goblins-tuned
narrowing removing a real decision branch from another deck, which the core invariant forbids
outright.

A provider per deck removes the whole failure mode: nothing is inherited unless it was written for
this deck, and "similar deck" stops being a routing decision at all.

## What "sharing parts" should mean

Not inheritance chains. The existing shape is already right — every archetype provider derives from
`GenericProvider` and overrides only the hooks it has measured evidence for — so sharing should be
**composition of the parts**, i.e. free functions and shared helpers that several providers call,
rather than one provider subclassing another and picking up hooks it never asked for.

The precedent to follow is the base cleanup-discard ranking itself: it lives on `DecisionProvider`,
not on `GenericProvider`, precisely because it is "the shared default an archetype narrows or widens
rather than a generic-deck-only answer". Shared behaviour belongs in a named, callable piece; a
provider opts in by calling it.

## The standing rule this does NOT replace

**A deck earns its own provider once it has a MEASURED hook to hold** — not before. Until then it
routes to `GenericProvider` and gets no narrowing at all, which is the side that cannot silently
cost anything. Dragons is the worked example in both directions on the same day: routed to Generic
when its GoblinsProvider misroute was fixed *because* it had nothing measured, then given
`DragonsProvider` hours later once its cleanup-discard bucket policy was authored, approved and
measured (net -4 turns over 3,500 suite games).

So the direction is not "create sixteen empty providers". It is: when a deck needs a heuristic, it
gets its OWN provider for it, and never borrows one.

## What "sharing parts" looks like in practice (2026-08-30)

The first real instance: `MinotaurProvider` and `DragonsProvider` both need to know what a subtype
cost reducer is. That is a free function, `IsSubtypeCostReducer`, which both call — NOT one provider
subclassing the other, and not each re-deriving the test. The re-derivation is the failure this
guards against: Dragons' own version read a param whose default made every card in hand a cost
reducer, and the policy shipped and was measured before anyone noticed. One shared predicate, taken
from the engine's own gate in `ManaPayment.cpp`, is both less code and the only version that can be
wrong in one place.
## Current state (2026-08-30)

`scripts/provider_audit.py` prints the live mapping. As of today:

* Own provider: AntiLifegain, Auras, Burn, CreatureGiving, Dragons, Dragonstorm, FiveColour,
  Goblins, Hinata2, KittyEquipment (Equipment), Minotaur, Mirrorwing, StompySurprise, treasure_hunt
* `GenericProvider`: none of the suite decks — Minotaur was the last one, promoted 2026-08-30 when
  its bucket-discard policy was implemented and measured
* **Shared across two DIFFERENT decks: Knights and slivers_vial both ride `VialProvider`.** Under
  this direction that is the one remaining case to split — they are not versions of one deck. It is
  deliberate and harmless today (both are Aether Vial decks and the hooks are Vial mechanics), so
  it is not urgent; it is listed because the direction says it should eventually be two providers
  sharing the Vial parts.

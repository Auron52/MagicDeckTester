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

## THE OBSTACLE, and why it is worth fixing properly rather than working around

**"When we don't know what the top 1-2 cards are" has no referent in the CLAIRVOYANT search.** That
search can see the library, so there is no uncertainty to condition a rank on: a rule written as "we
don't know the top" evaluates to "we always know" and never fires as intended -- the same class of
mistake as reading a null from a lever nobody verified fires.

A *proxy* is available and would work today: "was the top deliberately ARRANGED (a keep, not a
shuffle)?" is a property of the game rather than of an agent's information. But the USER's ruling
(2026-08-28) is not to reach for the proxy: **"frankly we'll need to implement this at some point
anyway, for non-clairvoyant players."** That is correct, and the reason is stronger than this one
cast-order rule.

### The non-clairvoyant search DESTROYS knowledge the player legitimately has

`MTG_NC_SEARCH` / `TurnSolver::ReshuffleAvgChoosePlan` models non-clairvoyance by reshuffle
averaging: per sample it does

```cpp
GameState s = state;
s.ActivePlayer().library.Shuffle(rs);      // the WHOLE library -- no preserved known prefix
```

Shuffling the whole library is right for the unknown remainder and WRONG for any prefix the player
has legitimately seen. A scry that puts a known card on top, or a Ponder that KEPT its three, is
real information a real player carries into the next decision -- and NC deletes it before evaluating
anything. Consequences, in rough order of severity:

* NC cannot see the payoff of its own card selection ACROSS turns. Arrange the top now, and by the
  next decision the arrangement is gone, so the benefit never appears in a sampled future.
* It therefore systematically under-values scry / reorder / keep effects -- on a deck whose engine is
  sixteen cantrips.
* Any heuristic conditioned on "the top is known" is unmeasurable under NC until this is fixed,
  which is exactly why the ruling above sequences the knowledge model FIRST.

(Within a single sample NC does still value *casting* a cantrip -- it shuffles, then applies the
plan, so the keep/shuffle choice operates on that sample's top. The defect is about knowledge held
BEFORE the decision, not about the cantrip's own draw.)

### The principle: MONTE CARLO IS FOR UNCERTAINTY ONLY

USER, 2026-08-28: *"there are always some cards that are known by a normal player, due to library
manipulation. Those should not be treated using monte-carlo, because we know what they are."*

That is the whole specification, and it is a correctness statement rather than a tuning one. The
library splits into two parts and they need opposite treatment:

| part | what it is | correct treatment |
|---|---|---|
| KNOWN prefix | cards this player has legitimately seen and not yet drawn | evaluated EXACTLY -- never resampled |
| unknown remainder | everything else | reshuffle-averaged over K samples, as today |

Averaging over the known prefix is not a conservative approximation of it -- it is strictly wrong,
because it replaces a fact with a distribution and then averages away the very thing the player paid
a card to establish. A normal player is not uncertain about the card they just scried to the top.

Note the state is NOT Hinata-specific and not cantrip-specific. Any library manipulation leaves a
known prefix: scry, surveil, a Ponder/Brainstorm-style reorder, a fetchland's search, a reveal, and
the top card exiled-and-playable by Soulfire Eruption (already modelled separately as a staged card
with an expiry). Whatever this deck does with `cast_reorder` / `cast_scry`, the field belongs on the
player, not on a provider.

### What the model needs to be

A **persistent known-top count**, not the turn-scoped field an earlier draft of this note proposed:
knowledge from a scry survives until those cards are drawn, or until something shuffles the library
(a Ponder that SHUFFLES resets it to zero -- that is the branch's whole meaning). Then:

* `ReshuffleAvgChoosePlan` shuffles only BELOW the prefix, per the table above.
* `CastOrderRank` -- which already takes `GameState` -- can finally ask "is the top known?", making
  the interleave rule at the top of this note expressible as stated rather than by proxy.

One field, two consumers, and the NC one is a correctness fix independent of any cast order. That is
the argument for building it once and properly instead of proxying it here.

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

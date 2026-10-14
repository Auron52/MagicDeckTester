# The Mycotyrant: implementation scope, and what it actually argues for

**Status: IMPLEMENTED 2026-10-14.** In `src/cards/data/cards.json` (533 cards), unit-tested
(`test/unit/test_mycotyrant.cpp`), smoke-clean and byte-identical for every existing deck. Read the
**BUILT** section at the end of this file FIRST: four things in the resolved design did not survive
contact, and one of them -- the claim that tokens cannot reach the graveyard -- was simply false and
would have made the card's weakest clause look like its strongest.

Everything below the first divider is the SCOPE as written before any code existed, kept because the
strategic argument in it is still the reason the card is here at all. It gates the Fungus mono-green
decision, so this file exists to stop that decision being made on a guess about the card.

## The card (Scryfall, fetched 2026-10-13 -- not recalled)

```
The Mycotyrant            {1}{B}{G}     Legendary Creature - Elder Fungus      */*
                          mythic, The Lost Caverns of Ixalan; modern/legacy/vintage/commander legal

Trample
The Mycotyrant's power and toughness are each equal to the number of creatures you
control that are Fungi and/or Saprolings.
At the beginning of your end step, create X 1/1 black Fungus creature tokens with
"This token can't block," where X is the number of times you descended this turn.
(You descend each time a permanent card is put into your graveyard from anywhere.)
```

It is itself a **Fungus**, so it counts toward its own P/T (minimum 1/1 alone), is pumped by
Sporecrown Thallid's Fungus lord, and receives a Sporesower Thallid spore counter each upkeep
that it can never spend -- the same faithful-and-free accumulation Mycoloth and Sporecrown
already have. It is **Legendary**, so the legend rule caps the battlefield at one; extra copies
are draws that do nothing, which bounds any sensible count at 1-2.

## THE THING MOST LIKELY TO BE MODELLED WRONG: descend counts CARDS, not tokens

> *"You descend each time a permanent **card** is put into your graveyard from anywhere."*

A token is not a card. **In the deck that wants this card, the entire token economy -- around 40
Saprolings a game -- contributes exactly ZERO descend.** Every outlet that eats a Saproling
(Utopia Mycon, Psychotrope Thallid, Vitaspore Thallid's haste grant, Mycoloth devouring tokens)
is descend-silent. Getting this backwards would make the end-step clause look like the best card
in the deck instead of a near-dead rider.

Nor is it cumulative: X is *"the number of times you descended **this turn**"*. So X resets every
turn, and the real sources in candidate-b are a trickle of one-offs:

| source | copies | descend events | shape |
|---|---|---|---|
| Peat Bog | 4 | 1 each, on the turn it depletes | land card sacrifices itself |
| Hickory Woodlot | 4 | 1 each, on depletion | same |
| Saproling Burst | 4 | 1 each, on fade-out | enchantment card sacrificed |
| creature CARDS dying | -- | 1 each | devoured Thallids, Tukatongue, Myconid |
| Saproling TOKENS dying | ~40 | **0** | not cards |

So X is realistically **0 or 1**, occasionally 2 when a land depletes on the same turn a Burst
fades. The end-step clause is worth roughly one 1/1 on some turns -- not an engine.

## What the card is actually worth here: the CDA

P/T = creatures you control that are Fungi **and/or Saprolings**. Saprolings count, and this deck
makes dozens, so this is a three-mana creature that is routinely an 8/8 or larger. That is the
whole card in this archetype, and it is a real rate. Note it compounds mildly with its own
tokens (each 1/1 Fungus token raises its P/T by one) and with Sporecrown Thallid.

**Trample is goldfish-inert** -- the passive opponent never blocks, so trample can never change an
outcome. Same for the tokens' *"This token can't block"*. Both are disclosure-only (`[PARTIAL: ...
WHY provably inert]`), per the convention Archangel of Thune's flying and Ocelot Pride's first
strike already follow. The tokens being **Fungus and not Saproling** is NOT inert: it means the
deck's three "Sacrifice a Saproling" outlets cannot eat them.

## THE STRATEGIC CORRECTION this file exists to make

Earlier notes in this project called The Mycotyrant *"the strongest argument FOR keeping black."*
With the real text in hand that is **too strong, and in a specific way**:

* Its payoff is **colour-agnostic** -- a big body scaling off Fungi/Saprolings, which the green
  half of the deck produces. Nothing about the payoff needs black.
* The descend clause, the one genuinely black-flavoured mechanic on the card, is **close to dead
  in a token deck** (above). The appealing "Peat Bog sacrifices itself, so the black mana feeds
  the black card" synergy is worth about one 1/1 on the turns it happens.
* What it really needs is **one black pip, once**. It raises the count of cards wanting `{B}` from
  3 of 40 to 4 of 40.
* **Secluded Courtyard can cast it** -- the Courtyard's `colored_creature_only` restriction is the
  deck's main complaint about it, and this is a creature, so that restriction does not bite here.

So the honest claim is narrower and more useful: **The Mycotyrant argues for keeping a black
SOURCE, not for keeping the black PACKAGE.** That maps directly onto the round 2 decomposition
(black cards / black mana / Courtyard's restriction): if round 2 says the black *cards* are dead
weight but a light black *mana* base is fine, The Mycotyrant is the one black card that earns its
pip -- and it needs one or two sources, not Peat Bog x4.

**Corollary for reading round 2: its verdict is conditional on this card's absence.** A "black is
not helping" result cannot settle mono-green on its own, because the strongest black card in the
pool is not in the list being measured. That conditionality is the reason this card gates the
decision -- not an assumption that it is a bomb.

## Build scope (every hook verified in-tree, 2026-10-13)

1. **CDA P/T -- a new param, with two close precedents.** `CardDatabase.h` already has
   `power_equals_creature_count` (676), `pt_equals_snow_permanents_you_control` (703) and
   `pt_equals_snow_permanents_on_battlefield` (706). Needed: a subtype-filtered creature count,
   e.g. `pt_equals_creatures_you_control_subtypes: ["Fungus", "Saproling"]` (count a creature ONCE
   even if it is both). It MUST be read through `DynamicBasePower` / `DynamicBaseToughness` --
   `DecisionProvider.h:581` warns explicitly: *"MUST read power through DynamicBasePower, not a
   static P/T"*, and the read sites are already wired (`Combat.cpp:25,159`, `main.cpp:2710`,
   `DecisionProviders.cpp:5010`).
2. **End-step tokens -- the block already exists and needs no new plumbing.** `endstep_lifegain_tokens`
   + `endstep_tokens_unconditional` + `endstep_token_power/_toughness/_color/_subtypes/_keywords`
   (`CardDatabase.h:2049-2088`), resolved by the shared `PerformEndStepLifegainTokens`. Its own note
   states the hard requirement: called **identically from `GameEngine::EndStep` (executor) and
   `TurnSolver::SimulateEndAndStartNextTurn` (rollout)** -- *"lockstep, or the rollout projects a
   board the real game keeps growing every turn (a systematic fd-diverge)"*. What is new is only
   that the COUNT is dynamic (X), where every existing user has a fixed N.
3. **Descend is genuinely new** -- nothing in `src/` implements it (every grep hit for "descend" is
   the English word in a sort comment). Needed: a per-turn `descended_this_turn` counter on
   `PlayerState`, incremented wherever a permanent **card** enters the owner's graveyard **from any
   zone** (battlefield deaths and sacrifices, the self-sacrificing depletion lands, Saproling
   Burst's fade-out, and discard/mill if ever added). Over-approximating is NOT safe here -- a
   token must not increment it.
4. **Param-gate the state-key fold, following `life_gained_this_turn` exactly.** `GameState.h`
   around 362-394 shows the established pattern: a per-turn counter that a card READS from the
   battlefield gets a per-deck gate (*"0 = no such card"*, converted from a bare bool) so it is
   folded into the key only for decks that need it, keeping every other deck byte-identical. The
   comment at 374 names the reason a read counter must be in the key at all: two states with
   different counters *"have DIFFERENT MANA AVAILABLE"* -- i.e. a live divergence.
5. **`static_assert(sizeof(GameState) == 1080, ...)` at `Dominance.h:391` will trip.** That is the
   guard working; update the number and the maintenance log beside it, and add the fold in
   `dominance::Build()` next to `lands_played_this_turn` / `cards_drawn_this_turn` /
   `life_gained_this_turn`.
6. **Route via `.claude/skills/analyze-deck.md` + `.claude/skills/mtg-rules.md`**, and note that
   `scripts/deck_compare.py` refuses a card that is not in `cards.json` or whose implementation has
   gaps -- so this must land before any screen arm can hold it.

## Open question for the USER -- ANSWERED 2026-10-13 ("Then add The Mycotyrant.")

Whether to implement it at all is worth deciding on the above rather than on enthusiasm: the
descend half is near-dead in this archetype, so what is being bought is a 3-mana creature that is
often 8/8+, for one black pip, capped at one copy in play by the legend rule. That is a good card
-- but it is not a reason to keep Deathspore Thallid, Slimefoot or Peat Bog x4, and it should not
be allowed to retro-justify the black package that round 2 is measuring.

---

# RESOLVED DESIGN (2026-10-13) -- USER said "Then add The Mycotyrant."

The scope above was written before the hook sites were traced end to end. Three decisions were open
and all three are now settled; the one that changed materially is descend.

## 1. The CDA P/T -- as scoped, one new param, one new helper

`DynamicBasePower` / `DynamicBaseToughness` (`SpellEffects.h:15824` / `:15837`) are a flat param
dispatch, so this is additive and every other card is byte-identical:

```
pt_equals_creatures_you_control_subtypes: ["Fungus", "Saproling"]
```

A new `CreatureSubtypeCount(state, controller, subtypes)` beside `CreatureCount`
(`SpellEffects.h:15194`), counting each creature ONCE even when it matches several subtypes -- the
card says "that are Fungi and/or Saprolings", and The Mycotyrant's own Fungus tokens plus the
deck's Saprolings make double-matching bodies reachable, so this is not a theoretical concern.

Printed P/T is `*/*`, i.e. 0/0 before the CDA, which means the SBA toughness check is load-bearing:
alone on an empty board it is a 1/1 (it is itself a Fungus), and `DynamicBaseToughness` must be read
by the SBA or it dies on resolution. The read sites are already wired -- `Combat.cpp:25,159`,
`main.cpp:2710`, `DecisionProviders.cpp:5010`, plus `SoulbondCandidatePower` and `CombatPowerOf`.

## 2. The end-step tokens -- the block exists; only the COUNT is new

`PerformEndStepLifegainTokens` (`SpellEffects.h:6338`) is called in lockstep from
`GameEngine.cpp:843` (executor) and `TurnSolver.cpp:42970` (rollout). The Mycotyrant's trigger has
NO intervening-if -- X is simply 0 on a turn with no descend -- so it sets
`endstep_tokens_unconditional: true`, which candidate-b ALREADY arms via Brightcap Badger
(`deck_has_unconditional_endstep_tokens`), so the early-out at `:6359` does not need touching.

One new param, `endstep_tokens_equal_descend: true`, replaces the fixed loop bound with
`DescendedThisTurn(state, active)`. The trigger-collection gate at `:6368` tests
`endstep_lifegain_tokens > 0`, so it must learn the new param too or the card never enters the
`triggers` vector -- the same shape of half-fix as the `SecondMainUnproductive` gate that nearly
un-fixed the Mycoloth pin. Token spec: power/toughness 1, colour `B`, subtypes `["Fungus"]`.

**The tokens are Fungus, NOT Saproling, and that is not cosmetic:** the deck's three "Sacrifice a
Saproling" outlets cannot eat them, while they DO raise The Mycotyrant's own P/T. *"This token can't
block"* is `[PARTIAL: provably inert -- the passive opponent never attacks, so we are never offered
a block]`, the same disclosure convention Archangel of Thune's flying carries. Trample likewise.

## 3. DESCEND -- the scoped design does not survive contact, and here is the replacement

The scope said: *"increment wherever a permanent card enters the owner's graveyard from any zone."*
**There are 76 `graveyard.push_back` sites in `src/` and no chokepoint.** Instrumenting all of them
(or refactoring them behind a new helper) is a wide, regression-prone change to a mature engine for
one card, and the failure mode is silent under-counting.

**Instead: derive it from a turn-start snapshot.**

```
PlayerState::gy_permanent_cards_at_turn_start   // refreshed at turn start
DescendedThisTurn(state, pi) = max(0, PermanentCardsInGraveyard(state, pi) - snapshot)
```

This is COMPLETE for every zone by construction -- it reads the destination, so battlefield deaths,
the self-sacrificing depletion lands, Saproling Burst's fade-out, and any future discard or mill all
count without a single call-site edit. Tokens are excluded automatically: a token ceasing to exist
never reaches the graveyard vector, which is exactly the rule the card needs
(*"a permanent **card**"*) and the thing the scope doc flagged as most likely to be modelled wrong.

**The one inexactness, stated plainly:** a card that LEAVES the graveyard and returns in the same
turn is counted once rather than twice. `[PARTIAL: WHY inert here --` nothing in this pool moves a
card out of a graveyard (no recursion, no flashback, no escape, no exile-from-graveyard), and the
param gate means no other deck reads the counter at all.`]` If a recursion card ever lands in a deck
that also holds a descend reader, this must become real event counting.

## 4. The memo/dominance key -- use the mechanism that already exists

`GameState.h:83` defines `enum GyReader` with the standing rule *"any new graveyard-reading path
must add one"*, and `Dominance.h:934` folds the graveyard ONLY through those bits. Fungus currently
sets none, so **two states differing only in graveyard contents share a key today** -- which is
correct now and wrong the moment a descend reader is on the battlefield (sacrifice a Peat Bog or
not, and the end step makes a different number of tokens).

So: `GyR_DescendCount = 1u << 7`, set by the `GoldFishRunner.cpp:644` audit when any deck card
carries `endstep_tokens_equal_descend`, folding the PERMANENT-CARD COUNT of the graveyard plus
`gy_permanent_cards_at_turn_start`. Those two operands determine the descend count exactly, so the
fold is exact rather than conservative, and it is gated -- every deck without the card keeps a
byte-identical key. `deck_has_descend_readers` is NOT a separate stamp: the GyReader bit is the gate.

`static_assert(sizeof(GameState) == 1080)` at `Dominance.h:396` will trip when the snapshot int
lands. That is the guard working: update the number and the size ledger beside it. Prefer placing
the int where existing padding absorbs it, as `deck_reads_lifegain_in_play` documents at
`GameState.h:387`.

## 5. Count, and what it replaces

Legendary, so the legend rule caps the battlefield at one and the sensible range is **1-2 copies**
(USER 2026-10-13: *"more copies is primarily insurance to make sure you have one and 2 is a number
that only rarely gets duplicates"* -- and the draw-consistency channel that argument rests on IS
fully modelled by a goldfish; only redundancy-against-removal is not).

**The slot: PSYCHOTROPE THALLID, not Deathspore** (superseded 2026-10-13 by round 8, after this
section was first written). Round 8 priced every slot in the list at ONE copy and found Psychotrope
Thallid 1->0 measuring **-0.0123 std / -0.0180 2hg** -- cutting it GAINS in both formats, so it is a
whole free slot and strictly better than the Deathspore slot round 4 had identified as merely free
(-0.0003/-0.0008). It is also the only choice that does not create a conflict: the user has since
ruled a hard FLOOR of one Deathspore, leaving exactly one spendable Deathspore copy, and that copy
is wanted for a second Slimefoot. Secluded Courtyard CAN cast The Mycotyrant (its
`colored_creature_only` restriction does not bite on a creature), and black stays at 3 of 40 cards
-- so the black package is not expanded to pay for it, which is the trap this file's strategic
correction warns about.

---

# BUILT (2026-10-14) -- four places the resolved design did not survive contact

Implemented, unit-tested, smoke-clean (125/125 PASS, 0 configs changed -- byte-identical for every
existing deck) and verified in 40 logged games. Four things differ from the section above, and the
second one is a correctness finding rather than a refinement.

## 1. Descend snapshots the graveyard's SIZE, not its permanent-card COUNT

The resolved design said `gy_permanent_cards_at_turn_start` plus
`max(0, permanent_cards_in_gy_now - snapshot)`. Shipped instead:
`Player::gy_size_at_turn_start`, with `DescendedThisTurn()` counting the permanent cards in
`graveyard[snapshot .. end)`.

Both forms are EXACT under the same condition -- the graveyard is append-only within the turn -- and
degrade identically when it is not. The size form is **O(cards that arrived this turn)**, usually
zero, where the count form needs a full graveyard walk with a `LookupCached` per card at every read.
That matters because one of the readers is `BuildSimKey`, ~15% of runtime on the one deck whose
sacrifice engine makes the graveyard the fastest-growing zone on the board.

It also turned out to be the form that could absorb finding 2 at all: a per-card suffix scan can
filter tokens, where a count difference cannot.

## 2. TOKENS ARE NOT EXCLUDED FOR FREE -- the central claim above is FALSE

The resolved design's argument for deriving descend from the destination zone was: *"tokens are
excluded automatically: a token ceasing to exist never reaches the graveyard vector."* **It does.**
Of the ~45 `graveyard.push_back` sites in `SpellEffects.h` only a handful test `is_token` first; the
rest push unconditionally.

Found in play, in the first 24 logged games, not by re-reading the code: `Vitaspore Thallid`
sacrificed Saproling token **#1000**, the token's `Card` landed in the graveyard, and under a
Doubling Season the end step made **two** tokens on a turn whose true descend was **zero**. Across
40 games, **9 token cards** appeared in graveyards -- so this is routine, not a corner. In a deck
that sacrifices ~40 tokens a game an unfiltered count would not be slightly wrong, it would be the
entire card.

Fixed by filtering at the READ: a graveyard card is not a CARD when
`m_number >= GameState::kFirstTokenNumber` (1000). Exact in both directions by construction -- every
token-creation helper draws its id from `next_token_number`, and deck cards are numbered 1..N -- and
it is the test several other sites in `SpellEffects.h` already open-code.

**LEFT OPEN, AND IT IS A REAL BUG INDEPENDENT OF THIS CARD.** A token that leaves the battlefield
ceases to exist (CR 111.7) and should never persist in a graveyard. It does, at every unguarded
site. That is not fixed here because `graveyard.size()` is a feature of the learned mid-game
evaluator (`MidGameFeature::GraveyardSize`), so emptying tokens out of the zone would move play on
every deck carrying such a model -- a GT-moving change that needs its own A/B and is the user's call,
not a side effect of adding a card. Filtering at the read is local, gated, and provably right for
this card. If it is ever fixed at the sites, the id filter here becomes redundant but stays correct.

## 3. One shared predicate, not three widened tests

`endstep_lifegain_tokens > 0` was doing double duty as "how many tokens" AND "does this card have an
end-step token trigger at all", in THREE places: the trigger-collection gate, that loop's bound, and
`GoldFishRunner`'s `deck_has_unconditional_endstep_tokens` stamp (which is conjunctive --
`endstep_tokens_unconditional && endstep_lifegain_tokens > 0`). A card whose count is dynamic answers
0 to all three, so it would never be collected, never fire, and never arm the stamp that keeps the
hoisted lifegain early-out from swallowing it -- which is the Brightcap Badger bug exactly.

All three now route through `HasEndStepTokenTrigger(params)` / `EndStepTokenCount(params, state,
controller)`, so they cannot disagree. The resolved design named only the first of the three sites.

## 4. The key folds cost nothing, and no size assert moved

`GyR_DescendCount = 1u << 7` as designed, set in `DeckGraveyardReaders` for any card carrying
`endstep_tokens_equal_descend`, and it IS the gate -- there is no separate `deck_has_descend_readers`
stamp. Two refinements:

* **Both key folds take the two OPERANDS** (graveyard size, turn-start baseline) rather than the
  derived descend count: O(1) instead of a walk, and strictly FINER than the count (it also
  separates a turn that put an instant in the graveyard from one that put a permanent there, which
  descend does not care about). Finer costs memo hits, never soundness.
* **Gated on the bit for BOTH players**, unlike `BuildSimKey`'s two neighbouring graveyard folds,
  which are restricted to `pi == 0` because the opponent's fixed 60 can genuinely contain the cards
  they test for. This fold's only consumer reads `DescendedThisTurn(state, active_player_index)` off
  a permanent WE control, so a clear bit proves nothing in the game reads either player's descend.
  Always folding for `pi == 1` would add a fold to every deck in the repo and move all their keys.

`static_assert(sizeof(GameState) == 1080)` did NOT trip, and that was deliberate rather than lucky:
the new int was declared immediately after `Player::life`, in the four bytes of padding that preceded
`hand`'s 8-byte alignment. Both layouts were compiled and printed -- beside the graveyard or the
per-turn counters it is 208/1096, at offset 4 it is 200/1080 -- because `GameState` is deep-copied at
every search node on the heaviest deck in the suite. The size ledger in `Dominance.h` records the
zero-delta entry anyway, per its own rule that the log is the record and the assert only its alarm.

## What is asserted, and where

`test/unit/test_mycotyrant.cpp` (8 cases): the printed characteristics the CDA depends on; the CDA on
both axes, counting itself, tokens, and a second Fungus card, and NOT a Badger or the opponent's
board; the one-pool rule for a creature that is both Fungus and Saproling, order-independently; the
toughness SBA reading the CDA (a lone Mycotyrant is a 1/1, and `LethalToughness` says 1 where
`EffectiveToughness` says 0); descend's baseline, its nonpermanent filter, its per-player scope, its
underflow clamp, the token filter with the trap asserted (a token's leftover Card passes the
permanent-type test -- only the id separates it), and the `ZoneCard` requirement with both halves
spelled out; the presence-vs-count predicate for all three cards in the family; the end-to-end end
step (X tokens, 1/1 black Fungus, none on a descend-free turn, each token raising the CDA, and the
hoisted early-out documented); and `GyR_DescendCount` being stamped for a list holding the card and
not for the rest of candidate-b.

In play, over 40 logged games on a 4-copy verification list (`logs/`, never committed): 30 end steps
with a Mycotyrant on the battlefield, 11 with X > 0, **0 mismatches** against an independent
recomputation of descend from the graveyard ids -- including the Doubling Season doubling -- and 11
snapshots where it was the only Fungus/Saproling on the board, surviving all 11.

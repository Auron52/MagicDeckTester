# The Mycotyrant: implementation scope, and what it actually argues for

**Status: SCOPED, NOT IMPLEMENTED.** Not in `src/cards/data/cards.json` (532 cards as of
2026-10-13). It gates the Fungus mono-green decision, so this file exists to stop that decision
being made on a guess about the card.

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

## Open question for the USER

Whether to implement it at all is worth deciding on the above rather than on enthusiasm: the
descend half is near-dead in this archetype, so what is being bought is a 3-mana creature that is
often 8/8+, for one black pip, capped at one copy in play by the legend rule. That is a good card
-- but it is not a reason to keep Deathspore Thallid, Slimefoot or Peat Bog x4, and it should not
be allowed to retro-justify the black package that round 2 is measuring.

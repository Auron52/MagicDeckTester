# A fetchland's measured value depends on the SHUFFLE MODEL, not the card (deferred investigation)

Status: **open, deferred** (found 2026-10-22 during the SelesnyaLifegain list screen). No engine
change has been made. Until this is explained, a screen result for a fetchland — or any card whose
value comes through a mid-game library shuffle (fetches, tutors, Ponder-style shuffles) — is not
trustworthy as a deckbuilding signal.

## The observation

SelesnyaLifegain screen, round 5 (`logs/selesnya_screen/`, seed 2650000, 10,000 paired games per
cell, d5 / b20, std and 2HG). Reference list `m0` holds 4 Forest and **no Plains and no Forest- or
Plains-typed dual**, so a Windswept Heath in it can only ever fetch a basic Forest. In paper that
Heath is a Forest plus one card of deck thinning, i.e. worth roughly nothing.

The engine says otherwise, and the size of the "gain" tracks the shuffle model:

| shuffle model (global flag) | Forest -> Windswept Heath, std | 2HG |
|---|---|---|
| `MTG_NO_SEARCH_SHUFFLE=1` (no post-search shuffle; thinning only) | **-0.0031 +- 0.0046** | -0.0019 +- 0.0048 |
| default (CRN `Library::ShuffleByKey`) | **-0.0280 +- 0.0047** | -0.0295 +- 0.0050 |
| `MTG_LEGACY_SHUFFLE=1` (search-index-keyed Fisher-Yates) | **-0.0402 +- 0.0047** | -0.0453 +- 0.0050 |

`m0` itself never searches its library, so its games are byte-identical across all three models;
every number above is a clean paired difference against the same control games. Thinning alone
(the no-shuffle row) is the null it should be. **A fair reshuffle should not make a deck faster in
expectation, yet it is worth ~0.03-0.045 turns here** — about as much as the best single card swap
found in that round.

## What was ruled out (and what was not, cleanly)

* **Not card modelling.** `PerformFetch` (`src/core/SpellEffects.h`) pulls the first matching library
  copy, puts it onto the battlefield, pays 1 life and calls `ShuffleAfterSearch`. (The Windswept
  Heath `oracle_text` note still says "no shuffle modelled" — that is STALE; the shuffle has been
  default-on since `SearchShuffleEnabled()` flipped.)
* **Search-side clairvoyance through the salt is NOT confirmed either way.** With
  `MTG_SHUFFLE_SALT_SEARCH=1` (so the rollout's reshuffle should differ from the executor's), the
  Heath delta moved by only +0.0002 +- 0.0003 (std) / -0.0003 +- 0.0005 (2HG): almost every game
  played identically. Either the decoupling instrument does not reach this path in batch play, or
  the search never plans past the shuffle (so it cannot be exploiting it). Settle this first:
  confirm with a single-game repro that the env var changes `state.shuffle_salt_search` in a batch
  job, and whether any rollout reads library cards after a fetch's shuffle.

## Hypotheses to test, in order

1. **Clairvoyant choice of WHEN to shuffle.** If rollouts do see post-shuffle draws in lockstep
   (by design, `ShuffleAfterSearch`'s deterministic seed), the search can choose between "play Heath
   (re-roll the future)" and "play a Forest (keep it)" knowing both outcomes. That is a free
   best-of-two that a real player never gets, and it would scale with how different the reshuffled
   order is from the original (legacy > CRN, which matches the table). Test: per-game, condition on
   whether the Heath was played on a turn where another land was also in hand (a real choice) versus
   forced; the gain should live entirely in the choice turns.
2. **The reshuffle undoes something non-random in the library order.** `Library::ShuffleByKey`
   resets `m_bottom_known` ("a shuffle forgets the known bottom pile"): the London-mulligan bottom
   pile returns to random positions. In paper that should HURT (the bottomed cards were chosen as the
   worst), so a positive gain would mean the bottomed cards are not the worst ones in this engine.
   Test: split games by mulligan count (0 vs >=1); a bottoming-driven effect lives only in >=1.
3. **Anything else that reads library order** (Genesis Wave reveals the top X; Wirewood/untap lines
   do not). Test by repeating the three-model table on a deck without library readers.

## Exposure

Every deck that holds fetchlands or tutors: `grep -l "fetch_land_types\|tutor_" src/cards/data/cards.json`
against `decks/*/`. Any past screen that measured a fetchland, a land-search spell or a shuffle
effect carries this bias in the direction of FAVOURING the shuffler.

## What was done in the meantime

The SelesnyaLifegain screen excludes Windswept Heath from every candidate list (the measured gain is
not attributable to the card). No other deck's conclusions were revisited.

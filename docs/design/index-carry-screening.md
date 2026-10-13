# Index-carry for screening: skip the games that cannot see the changed card

**Status: DESIGNED, NOT BUILT. Agreed 2026-10-13 to build AFTER the Fungus candidate-b round 2.**
USER's idea and USER's rulings throughout; this file records them so the build does not re-derive
them.

> *"keep track of the cards that are relevant (drawn, available) in each game and only replay the
> ones that have changed cards"* ... *"we would do it based on card index, so if index 50 is never
> accessible and that is the one we changed we can skip that and record the same result as the run
> before the change"* ... *"it would have to be inaccessible by all branches and all tutors"* ...
> *"Truly inaccessible."*

## What it is

A screen re-measures N games per arm against a base that differs by a handful of cards. For a
one-card edit most games never interact with that card at all, so their result is **provably
identical** to the base's and does not need simulating. Record, per game, which card INDICES were
accessible; if the edited indices were not among them, carry the base result.

**This is not a cache or an approximation.** If the changed index was truly inaccessible in game
`g`, then base and arm played the identical game, so the paired delta `D_g` is **exactly 0** --
known, not estimated. The estimator is byte-identical to the full run and `se` is still computed
over all N games. Pure compute saving, zero statistical cost.

## Why the existing feature does not cover it

`docs/design/execution-trace-carry.md` (Phase A+B BUILT 2026-07-07, `src/core/RolloutTouch.h`,
`MTG_KEEP_TRACE=1`) is the same instinct at the wrong granularity, and it is scoped to
*same-list / new-commit* changes. Three mismatches:

1. **Names, not indices.** It records card NAMES whose effects executed.
2. **Played, not encountered.** Its own note: *"'Played' is the right mask for a COMMIT change;
   'encountered' is required for a DECK change"* -- a game that drew but never cast the card ran
   none of the changed code (safe for a code change) but held a different card under a swap.
3. **Per-CELL union, not per-game.** A cell is reusable only if NONE of its R rollouts touched the
   card, so it saves `(1-p)^R` instead of `(1-p)`. Measured on burn: **0.00% for 8 of 10 cards**.
   The per-game mask was recorded there as the open lever (USER proposal, 2026-08-12) and is this
   document.

That doc's "Not for small deck changes" section says old masks "don't transfer cleanly" because a
decklist change alters library composition. **Indexing dissolves that objection** -- see below.

## Why indexing works: the invariant is already built

* `Library::ShuffleByKey` (src/core/Library.h) sorts by a key that is a **pure function of
  `(seed, m_number)`**, written deliberately so arms *"diverge ONLY by the specifically-different
  cards each removed, not by a full re-permutation"*.
* `inherit_numbering` (scripts/deck_compare.py) keeps every arm's number set at **{1..60}** and
  calls it the load-bearing invariant: *"two arms with the same number set sort them into the SAME
  order and position p holds the same number in both. Only the card AT a number can differ."* It
  inherits freed numbers from the END of the freed run so a one-card change moves exactly ONE
  number, and the union-pool alternative was measured worse and rejected (80.2% vs 95.8%
  opening-hand alignment, 2026-08-29).

So the drawn INDEX sequence is identical across arms; only the card at an edited index differs.

## The accessibility predicate -- "TRULY inaccessible"

A game may skip only if every edited index is unreachable by ALL of:

1. **Draws / opening hand**, across every mulligan taken (each mulligan reads its own permutation,
   so the mask is the UNION over mulligans, not the kept hand alone).
2. **Every search branch.** Because the search shares the game's shuffle (`state.shuffle_salt`),
   all branches read the SAME library order, so the union over branches is a **PREFIX**. Record the
   deepest prefix any branch read. (A search that re-shuffled per branch would make every game
   sensitive to every card -- `shuffle_salt_search` is the opt-in decouple instrument and must
   disable the carry.)
3. **Anything that left the library** -- hand, battlefield, graveyard, exile, and bottomed cards
   from mulligan bottoming.
4. **Tutors (USER 2026-10-13).** A tutor searches the whole library, so it breaks the prefix
   property. Handling, per the USER: *"check whether there is a tutor that can find your card (if
   there is a pruning heuristic that could also prevent it) and then also play the games that have
   those tutors."* So:
   - compute per-edited-card **tutor reachability**: does any tutor in the list have search criteria
     the edited card satisfies?
   - if yes, do NOT skip any game in which such a tutor was itself accessible. Games where no
     matching tutor was reachable remain skippable.

**Over-approximate, always.** Any route, zone or effect the analysis does not model counts as
ACCESSIBLE. The failure direction is asymmetric and vicious: a false "inaccessible" silently
substitutes a stale result while reporting a successful scoped re-run. That exact shape already
happened once in the sibling feature (2026-08-12: an empty touched-set is disjoint from every
changed-card set, so every cell read as provably-untouched and the ENTIRE prior would have been
reused; it was latent only because nothing shipped carried `traced`). Mirror the
`ProvenWinlessThisTurn` discipline: *"any card, zone or route the analysis does not cover returns
false"*.

## The pruning-heuristic trap (USER 2026-10-13)

> *"if we find a case with a pruning heuristic preventing access of our new card we would need to
> double check that before proceeding. (i.e. Is the heuristic correct on the new list?)"*

A tutor's candidate list is often NARROWED by a per-deck heuristic. If that heuristic never offers
the new card, the card looks tutor-unreachable -- but that inaccessibility is an **artifact of a
heuristic fitted to the OLD list**, not a fact about the new one. Skipping those games would hide
the very effect under test.

Live example in this repo: `MTG_PD_BESEECH_USEFUL` narrows Beseech the Queen to "engine creatures,
then the damage sources and Ancient Tomb, skipping duplicates". Add a card to Prevent Damage and
that rule may simply never offer it.

**Rule:** when inaccessibility depends on a tutor-pruning heuristic, the carry MUST NOT be taken
until the heuristic is re-validated on the new list. Treat it as a finding, not a saving -- and note
the upside: this analysis mechanically surfaces every case where a pruning heuristic excludes a
newly added card, which is a bug detector for the heuristic.

## Build notes

* Recorder: per game, ONE bit per card index -- nothing else. USER 2026-10-13: *"we can just store a
  64-bit integer (with 60 bit-flags) to represent which cards are accessible in a game. It doesn't
  need to be anything complex."*
  - **No prefix-depth field.** An earlier draft of this file stored the mask *plus* the deepest
    library prefix read. That is redundant: reading the top K cards sets those K cards' own bits, so
    the mask already represents the prefix.
  - **The test is one instruction:** `(accessible & changed) == 0`, with `changed` computed once per
    arm. That matters because it runs once per game per arm.
  - **Width:** one `uint64_t` covers a number space of 64. candidate-b is 60 main + 0 side (60 bits)
    and the shipped Fungus list is 60 + 4 = exactly 64, so Fungus fits in one word. **Nine decks do
    not** once the fetchable sideboard is numbered -- Hinata2 60+23=83, Prevent Damage 60+15=75,
    Soldiers and Bruna 72, Snow 70, Knights and EldraziDisplacerFlicker 68, Minotaur 67, Unpredictable
    Cyclone 65. `std::array<uint64_t,2>` covers 128 and so covers every deck in the repo, at two ANDs.
    Note the convergence: the decks that overflow one word are exactly the wish/tutor decks whose
    sideboard is reachable, i.e. the ones that need the tutor predicate anyway.
  - **Bit position is a COMPACTED index, not the raw number.** `base_numbering`'s docstring warns
    that *"contiguity is not required by the engine (m_number is only ever equality-compared, never
    an index)"*, and `inherit_numbering` hands out fresh numbers above the current high for a genuine
    size increase. So map number -> compact bit, exactly as `RolloutTouch::Sink` already maps name ->
    compact index. (In a 60-card screen every arm frees as many numbers as it adds, so the space
    stays 1..60 -- but the recorder must not assume it.)
  - Storage is ~8-16 bytes/game, i.e. irrelevant. Off by default and byte-identical when off (the
    `RolloutTouch` precedent: a null thread_local sink and one predicted-not-taken branch).
* Consumer: `scripts/deck_compare.py` already collects per-game results keyed by game index
  (`got[job][g]`) and already pairs arms against base on that index, so the carry slots into the
  existing pairing rather than needing new bookkeeping.
* **Verification mode is mandatory before trusting it:** run a round both WITH and WITHOUT the carry
  and assert the per-game results are **IDENTICAL**, not close. A sign test or a mean comparison
  cannot see a carry that silently reused the wrong game.
* Expected payoff, from `execution-trace-carry.md`'s measured per-game touch rates: ~50-65% of games
  skippable for a 4-of edit, ~75% for a 1-of, ~0% for a 20+-copy land. Fungus screening rounds are
  ~56,000-64,000 games each, so order 2x, and better the narrower the edit.
* Fungus is the easy first case: **no tutors, no mill, no shuffle effects** in either list; the only
  library-reader is Psychotrope Thallid's `sac_outlet_draw`, which merely extends the prefix.

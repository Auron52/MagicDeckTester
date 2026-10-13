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

(The ONE proposed rule that would weaken this to an approximation is the win-turn relaxation; it is
kept separate, off, and instrumented for exactly that reason -- see its section below.)

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
   all branches read the SAME library order, so the union over branches is a **PREFIX** -- set the
   bit of every card in that prefix (no separate depth field is needed; see Build notes). (A search
   that re-shuffled per branch would make every game sensitive to every card --
   `shuffle_salt_search` is the opt-in decouple instrument and must disable the carry.)
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

## The win-turn relaxation (USER proposal 2026-10-13) -- APPROVED AS A MEASUREMENT

**USER ruling 2026-10-13: *"Measuring it sounds good to me."*** So the two-mask recorder is part of
the FIRST build (retrofitting a per-turn snapshot later is the expensive order), the relaxed
predicate ships OFF, and the already-mandatory verification pass reports skip rate AND per-game
identity for every predicate. Adopting the relaxation is then a USER decision on that data -- it is
not pre-approved by this ruling, which authorises the measurement only.

> *"If we wanted to be even more tricky we could skip adding cards that are accessible only on the
> win turn, but not played as part of the winning line... it is technically correct in that it could
> change how we win, but not the winning turn."* ... *"Because the original line that wins on turn X
> is still there (so winning some other way on turn X is redundant information)."*

**The proof, and it is short.** The winning line never touched the edited index, so it is still legal
in the arm. A win is **terminal-maximal** -- nothing the substituted card enables can be worth more
than winning this turn. So the swap can only change WHICH maximal line is picked, and every maximal
line reports the same turn. Note what the argument does NOT need: no claim about the new card's
quality, only "the old line survives" + "nothing outranks a win."

**It is WINS-ONLY, and the exception is the dangerous direction.** For a game that hit the turn cap
without winning, "not played as part of the winning line" is vacuously true of every card -- there
was no winning line -- while the arm's different card on the final turn is EXACTLY what might have
won it. The relaxation is licensed by the win EXISTING, not by the turn being last. A build must gate
it on `won == true`, never on `turn == last_turn`.

**Under a binding budget the line is still there but may not be FOUND.** Screens run at
`budget_ms: 20`. This is the repo's standing distinction (`docs/design/budget-may-reprice-not-remove`
/ CLAUDE.md): re-pricing work is legal, making a line unfindable is not -- and a budget perturbation
is precisely a findability change. That splits the relaxation into two sub-cases whose risk differs
by a lot, and the split is the reason to build it:

1. **Library-only on the win turn** -- the edited index was read by the final turn's search prefix and
   never left the library. The root menu is **byte-identical**, so the winning action is enumerated in
   the same position, and rollout values cannot outrank a terminal win. Residual risk is only rollout
   unit accounting shifting the exhaustion point. **This is likely where most of the payoff is**: the
   win turn is where the search reads deepest, so the final turn's prefix extension is the single
   largest block of bits the strict mask sets.
2. **In hand (or any other zone) on the win turn, unplayed** -- the root menu itself differs, so
   enumeration and unit consumption differ. Sound at large budget; an approximation at 20 ms.

**Therefore: record it, report it, and let the data decide -- do not ship it on the argument alone.**
This would be the design's FIRST inexact rule, and it forfeits the "`D_g` is exactly 0, known not
estimated" property that the rest of this document rests on. It is also nearly free to measure:

* Recorder: snapshot the mask at the START of each turn, and OR in the cards the executor actually
  touched during the win turn. That is one extra `uint64_t` plus a per-turn copy; the skip test stays
  a single `AND`.
* Store the masks for all THREE predicates, which the verification pass evaluates side by side:
  - **P0 strict** -- the predicate the rest of this document specifies. Exact. The one that ships on.
  - **P1 relaxed, library-only** -- P0 minus the win turn's library-prefix extension, for won games
    only. Root menu byte-identical; near-exact; expected to carry most of the extra saving.
  - **P2 relaxed, full** -- P1 also minus cards that reached hand/another zone on the win turn and were
    never touched, for won games only. Sound at large budget, approximate at the shipped one.
* The mandatory verification run (below) reports, for each predicate: skip rate, AND per-game identity
  against the full run. Expected reading: P0 identical by construction (a failure there is a RECORDER
  BUG, which is the main thing the pass is for); P1 identical or near it; P2 is the real question. If
  P2 breaks identity, the finding is that the 20 ms budget is perturbing which line gets found -- worth
  knowing on its own, and a sharper instrument for it than anything currently in the tree.
* **Report the skip rates even if the identity check kills P1/P2.** The gap between P0 and P1 prices
  how much of the mask the final turn's search prefix is responsible for, which is a fact about search
  shape that nothing in the tree currently measures.

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
  - **Width: ONE `uint64_t`, always -- the mask covers the 60 MAINDECK cards only.** USER
    2026-10-13: *"I don't think we need the sideboard. Sideboards only represent cards reached from
    tutors in the list."* In this engine a sideboard is not a real sideboard (there are no games 2/3);
    it is purely the pool a wish/tutor fetches from, so a sideboard card is reachable ONLY via a
    tutor -- and the tutor predicate above already forces us to PLAY any game in which a matching
    tutor was accessible. So a sideboard card never needs a bit of its own, in either direction:
    swap a maindeck card and the sideboard is irrelevant; swap a SIDEBOARD card and reachability is
    decided by the TUTOR's maindeck bits, not the fetched card's.
    This retires an earlier draft's `uint64[2]`, which was sized for the nine decks whose
    main+sideboard exceeds 64 (Hinata2 83, Prevent Damage 75, Soldiers/Bruna 72, Snow 70,
    Knights/Eldrazi 68, Minotaur 67, Unpredictable Cyclone 65). Those numbers are real but
    irrelevant: none of those cards is reachable except through a tutor.
    (Aside: the shipped Fungus list has a 4-card sideboard and NO tutors, so that sideboard is
    unreachable outright -- consistent with Spore Flower not being in cards.json at all.)
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

---

## BUILT AND MEASURED, 2026-10-13 -- `"index_carry": true`

Both halves are in. `scripts/deck_compare.py` skips the games an arm provably cannot play
differently from base, and the saving is real:

**4 land-swap arms x 2,000 games, Fungus candidate-b (d5/b20, seed 1300000): 241 s carried vs 279 s
uncarried = 1.16x**, with 3,175 of 8,000 arm games skipped (39.7%). The delta / se / t / ident%
tables are IDENTICAL to the uncarried run on every arm, and a smaller paired test was checked PER
GAME across all 1,200 games.

### Skip rate falls fast in the number of CHANGED SLOTS -- so screen in increments of ONE

| changed slots | skip rate (measured) |
|---|---|
| 1 | **68%** (from the mask distribution) |
| 2 | **45-47%** |
| 3 | 32% |
| 4 | **21.6%** |
| 6 | 11% |
| 8 | 6% |

USER 2026-10-13: *"In the majority of cases we should probably move by increments of 1. That way we
get more numbers and high reusage."* The table is why: **halving the increment roughly doubles the
reuse**, so a 1-step ladder is cheaper PER ARM as well as more informative. Use the spec's `replace`
map on every arm -- it is what makes a 1-for-1 swap k=1 rather than k=2, by pairing the vacated slot
with the added copy so only ONE number's card differs.

### The engine half

Per-job `"skip_games": [<global gi>, ...]` (GLOBAL indices, matching the `[win]` lines). A listed
game is never enqueued and is stamped `kSkipped`, reusing the existing condemned-cell reduce path.
**The engine fabricates nothing** -- it does not write a win turn it did not compute; the driver
reconstitutes the series, because the soundness argument belongs to whoever owns the decklists.
Byte-identical for the games that do run (verified: the same 8 games, same win turns, with 4 skipped
alongside). `ParseJob` validates the list loudly -- an out-of-range entry is a hard error, because a
silently ignored one would read as "the carry skipped that game" while the game actually ran.

### Two preconditions, refused PER ARM

An ineligible arm simply runs in full, so turning the carry on can only make a run cheaper or
identical.

1. **The value model must not split on library COMPOSITION** (`lib_lands`, `lib_creatures`,
   `lib_damage_sources`, `lib_draw_engines`, `lib_land_density_pct`, `plan_dig_yield`). Read out of
   the model's own trees, not trusted to a doc.
2. **The deck-wide NONLAND coloured-pip vector must be unchanged.** `ChosenFloatColorCandidates`
   sums it over the library -- and `stable_sort`s the float-colour fan by DESCENDING magnitude, so
   presence is not sufficient and the full vector must match. Land-for-land arms pass automatically
   (its `scan` returns early for lands); most spell swaps do not, and are correctly refused with the
   moved colours named (`Mycoloth 2 -> Slimefoot 3`: `B 3->5, G 41->39`).

Deck-wide equality is SUFFICIENT, not merely necessary: a skipped game never draws the changed slots,
so those cards sit in the library all game and are counted in every zone that walk sums -- the totals
cannot drift apart mid-game.

### What keeps it sound as the engine changes

`carry_sites_unchanged()` greps the tree for `Library::RawAt` callers and REFUSES to carry if the set
has moved from `CARRY_RAWAT_SITES`. Every argument here is about a KNOWN set of unrecorded library
reads; a sixth one added later would make the skip unsound while every visible check still passed.
The count is deliberately crude -- it cannot tell a safe new site from an unsafe one, so it refuses
either way and makes someone write the argument down.

### Verification is unconditional

Every 50th otherwise-skippable game is HELD BACK and played anyway; a win-turn mismatch is a hard
failure naming the games, never a warning. Also forced to play: any game with `tutor=1` (a tutor can
name a SIDEBOARD card, which the 60-bit maindeck mask does not cover at all) and any with `wlr > 0`.

`<stem>.<label>.carryfill.json` persists the filled-in games and `--confirm` merges it. Without that,
a later confirm re-reading the deliberately-short `.err` log would pair on the PLAYED games only --
silently dropping exactly the games the carry proved were ties, which is the subset most favourable
to finding an effect. A performance feature turning into a measurement bias, invisibly.

### Two bugs worth recording, because both PASSED every soundness check while delivering nothing

* The verification stride counted with `len(skip)`, which never advances while the branch keeps
  choosing `sample` -- so `0 % N == 0` stayed true and ALL 88 skippable games went to verification.
  The run reported "0.0% skipped" and was perfectly sound. Verifying everything is not wrong, just
  worthless.
* The engine's per-job `remaining` counter was not reduced by the skips, so the job never completed,
  `on_job_done` never fired, and its `played=/ms=/units=` summary line was simply ABSENT. It surfaced
  only as `nan` in the ms/game column, with correct per-game results and correct deltas everywhere.

A performance feature that silently performs nothing is the failure mode to design against here.

### Still owed

* `Sink::BeginTurn()` has no turn-boundary hook, so the **win-turn relaxation** above is still
  unmeasured. It would raise the skip rate further.
* The wave barrier could be removed by moving the skip decision INTO the batch (base and arm jobs
  share a `gi`, so arm game `gi` becomes droppable the moment base game `gi` lands) -- one pool, no
  wave. At the measured scale the barrier costs little, so this is only worth it if that changes.

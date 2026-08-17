# Screening the Mirrorwing trick suite

**Status: UNBLOCKED 2026-08-17. Both blockers cleared; keep-table generation for the swap list is RUNNING. See "Progress" at the end.**
Written down because the sequencing decision has to be made BEFORE any generation is spent — the wrong
choice there costs a full table regeneration per follow-up tweak.

## The edit

`decks/Mirrorwing Dragon/` (60 cards). Replace the trick suite, keeping the copy shell intact:

| out | in |
|---|---|
| 4 Ancestral Anger | 4 Fortifying Draught |
| 2 Expedite | 4 Impolite Entrance |
| 2 Scale the Heights | 3 Luxurious Libation |
| 3 Twinflame | |

11 cards of 60 (18%), deck size preserved. Intended follow-ups afterwards are further single-card
tweaks within the same pool — the user named "Twinflame instead of Luxurious Libation" specifically.

## Blockers, in order

1. **Fortifying Draught, Impolite Entrance and Luxurious Libation are not in `cards.json`.** All three
   verified missing 2026-08-12. This is the `analyze-deck.md` + `mtg-rules.md` route: implement, review
   each against the rules skill, write. `deck_compare.py --preflight` refuses until then and prints
   the route.
2. **The deck has no apparatus.** `decks/Mirrorwing Dragon/` holds only the `.cod` and
   `.profile.json` — no `.value.json`, no keep table, no raw. Screening shares ONE table across arms;
   there is nothing to share yet. Value-leaf (`value-leaf.md`) and mulligan profile
   (`mulligan-profile.md`) come first, and the mulligan profile is commit-bound, so it comes last of
   those.

## What is NOT a blocker: provider identity (and since 2026-08-13, never a blocker for ANY edit)

Detection is `p.copies_solo_targeted_spells`, carried by Mirrorwing Dragon and Zada — both stay at 4,
so detection on the edited list agrees with the base anyway. Since 2026-08-13 that agreement is
reported, not load-bearing: every arm of a screen is pinned to the base deck's provider
(`MTG_PROVIDER_DECK`, inherited identity — see `deck-combination-screening.md`), so even an edit that
crossed a signature (the burn/Searing Blaze case) screens under the deck's own heuristics, with the
crossing printed as a NOTE.

## The decision to make BEFORE generating: build the table over the UNION, not the final list

Reweighting a shipped table to a new count vector is free (zero rollouts, seconds) but only works for
cards the table already buckets. An INTRODUCED card has no cells to reweight, which forces the pool-
table route — a full generation.

Twinflame is in the ORIGINAL list. If the keep table is generated on the FINAL list only, the planned
"Twinflame instead of Luxurious Libation" follow-up reintroduces a card the table never bucketed and
pays for a fresh pool table. Generating instead over a **union deck covering the whole candidate pool**
(both trick suites, plus any other card genuinely under consideration) makes every subsequent tweak
inside that pool a reweight.

The tradeoff is real: each extra distinct card raises K, and cell count grows faster than K. So the
union should be the cards that would actually be tried, not everything conceivable. Here the pool is
~6 out + 3 in, which is cheap beside regenerating per tweak.

## Caveats specific to an 18% edit

Every apparatus measurement to date is on 4-card swaps. Two things do not transfer:

- **The measured cell-value scatter does not.** `d = 0.054t` (burn, Skullcrack→Lightning Bolt) is a
  4-card figure; an 18% library change should move cell values considerably more. Run
  `scripts/keep_delta.py --arm ...` for THIS edit (~30 min, grid-size independent) and read the
  driver's `d*` against that, not against 0.054.
- **The new-bucket evidence does not stretch this far.** The pool table was shown unbiased in three
  cases, but all were K+0 or K+1 (`deck-combination-screening.md`). Here six buckets leave and three
  arrive. The union bias should be bracketed for this edit rather than assumed from those.

## Route once unblocked

1. Implement + review the three cards; `--preflight` the spec.
2. Value-leaf, then mulligan profile, generated over the UNION deck on a frozen commit.
3. Screen the suite swap; read `d*` against a `keep_delta` scatter measured for this edit.
4. Follow-up tweaks inside the pool are reweights.


---

## Progress (2026-08-17)

**Blocker 1 — the three cards — CLEARED.** Fortifying Draught, Impolite Entrance and Luxurious
Libation are implemented, reviewed and committed (`66729658`). Costs/oracle verified verbatim
against Scryfall; smoke 36/36 byte-identical, so all the new state and params are inert for the 17
existing decks.

**Blocker 2 — "the deck has no apparatus" — CLEARED and now STALE as written.** The deck has since
shipped a `.value.json` AND an adopted exhaustive keep+bottom table (generated 14h15m, adopted
2026-08-17, worth -0.20 turns at 0.256x cost), and all three regression modes were rebaselined
under it.

### What the sequencing advice got right, and what it got wrong

This doc said to generate over the UNION so later tweaks are free reweights, and warned the choice
must be made BEFORE spending generation. We generated over the SHIPPED list instead — but that was
not a mistake, because that table is the production artifact the deck now ships.

The doc's stated reason for the union ("an INTRODUCED card has no cells to reweight") is the weaker
half of the argument. The stronger one, from the user: **a cell's value is an average over the
library you draw from, so changing 11 of 60 cards moves the value of essentially every cell** --
including hands made entirely of unchanged cards. Structural reusability is not the binding
constraint; stale labels are. So a union table would not have avoided regeneration for a shipping
decision either.

What actually follows is the split the screening skill already encodes:

* **To SCREEN** (compare arms, try combinations): share ONE apparatus across arms. That is Rule 0,
  and it halves the se rather than approximating.
* **To SHIP** a winning list: generate on that list. One cost, paid once, at the end.

### K, measured rather than assumed

Equivalence discovery on the swap list: **K = 16**, nothing merges (the base is K=17 with 17
distinct cards, so nothing merges there either). Four distinct cards leave, three arrive.
Consequently the swap deck is **cheaper** to generate than the base: 144,630 size-7 cells against
202,878. Recorded as `value_play.expected_buckets: 16`; the generator's K guard caught the copied
17 and refused before spending hours, which is the guard working as intended.

### The bug this uncovered

Luxurious Libation was **never cast** — `{X}` spells whose template is not `DirectDamage` were
dropped from enumeration entirely. Found by neutralising the card's payload params and getting
bit-identical digests. Fixed; the card is now worth ~1.7 turns on an isolation deck. See
[[x-spell-tricks-dropped-from-enumeration]]. Had the screen run before this was caught, the swap
arm would have played as if holding three blanks and the incumbent would have "won" for a reason
that was not real.

### Still open

* **Impolite Entrance cannot be screened.** Trample is inert (no blockers) and sorcery-vs-instant is
  not modelled, so its `cards.json` entry is parameter-identical to Expedite. Any arm containing it
  measures "more Expedites", not the card. Its real merit is out of scope for this engine and stays
  a judgement call. (User-approved deferral.)
* **Luxurious Libation's token COLOUR** ("green and white") is not modelled — tokens carry subtypes
  and P/T only. Flagged for sign-off, not self-certified. Nothing in either list reads colour.
* **The union-bias bracket** this doc asks for still applies to whichever screen is run with a
  shared table, and `keep_delta` scatter should be measured for THIS edit rather than read against
  burn's four-card 0.054t.

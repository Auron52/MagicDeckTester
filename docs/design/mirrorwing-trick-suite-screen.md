# Screening the Mirrorwing trick suite (planned, blocked)

**Status: PLANNED, not started. Blocked on three unimplemented cards and on the deck's own apparatus.**
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

## What is NOT a blocker: the archetype signature survives

Detection is `p.copies_solo_targeted_spells`, carried by Mirrorwing Dragon and Zada — both stay at 4.
So both arms route to `MirrorwingProvider` and the driver's provider-split refusal passes. Worth
stating explicitly because the same check fails on a smaller-looking burn edit: `landfall_damage` IS
burn's signature, so cutting Searing Blaze drops that arm to `Generic`.

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

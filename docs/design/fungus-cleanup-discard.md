# Fungus cleanup discard: the generic rule sheds Mycoloth and Doubling Season first

**Status: OPEN.** Found 2026-09-23 when the USER asked *"Does fungus have a proper discard
heuristic? If it is searched there could be some issues"* and added *"(though there isn't that much
draw in the list, but our Psychotrope kept showing up in the slowest hands)"*.

## 1. It is NOT searched, and that half is fine

`DecisionProvider::CleanupDiscardSearchWidth()` returns the base **1**, and only
`TreasureHuntProvider` overrides it. `FungusProvider` overrides **nothing** discard-related, so:

* the rollout's plan enumeration emits no `discard_choice` variants (width 1 == no branch, and is
  byte-identical to not having the axis);
* the executor's searched cleanup pass (`AIEngine::ChooseDiscard`) trials exactly the indices
  `CleanupDiscardCandidates` returns, and the base implementation returns ONE.

So there is no search explosion here to worry about. That is also deliberate: a blind width > 1 was
swept and REFUTED on 2026-08-06 (monotonically worse at W=2/4/8 -- budget dilution, because the
decision fires on a tiny fraction of turns while every base plan pays a variant). If this axis is
ever widened it must be bp-style -- fan only the plans whose simulation actually reaches an
over-limit cleanup. See `searched-discard-as-search-node.md`.

## 2. The heuristic it DOES use is wrong for this deck

With no provider override, no `DiscardLandsFirst`, and **no `required_pieces` in
`decks/Fungus/Fungus.profile.json`** (its keys are only `card_scores`, `hand_score_threshold`,
`mulligan`, `version`), the base rule collapses to a single sentence: **shed the highest mana value.**

On Fungus that orders the hand exactly backwards:

| MV | card | shed order |
|---|---|---|
| 5 | **Mycoloth** | FIRST |
| 5 | **Doubling Season** | second |
| 4 | Sporesower Thallid | |
| 3 | Psychotrope Thallid, Beastmaster Ascension | |
| 2 | Thallid Shell-Dweller, Sporecrown Thallid | |
| 1 | Wild Growth, Utopia Mycon, Tukatongue Thallid, Thallid, Essence Warden | |
| 0 | Simic Growth Chamber, **Forest** | LAST |

A flooded hand therefore pitches the deck's two payoff cards and keeps the 19th Forest, in a deck
running **22 lands in 60**. Mycoloth is the card the whole second-main/devour strand
(`fungus-second-main-and-devour.md`) exists to cast.

## 3. Why it may still be small -- MEASURE BEFORE FIXING

The base rule's own note records that **five of nine suite decks never reach a cleanup discard at
all** and three more are under 40 per 400 d0 games. Fungus empties its hand onto the board, so the
over-seven cleanup may be rare; the deck's only draw is Psychotrope Thallid's sac-a-Saproling outlet.
**Do not write a provider override before counting how often the shed actually fires**, and count it
on a sample that contains the wide Doubling Season games -- §8c of `fungus-second-main-and-devour.md`
records that a 30-game Fungus sample inverts every cost/volume comparison the 150-game one gives.

Two distinct effects must not be conflated:
* a wrong shed costs **win turn** (quality);
* Psychotrope showing up in the slowest hands is a **cost** observation, and the likelier mechanism
  there is that each draw is a BREAKPOINT opening search nodes, not the discard rule.

## 4. FREQUENCY, MEASURED

`MTG_TRACE=discard` (real resolutions only -- every rollout scope clears `g_real_resolution`, so
this counts the discards the GAME made, not the millions the search imagined), 400 games at d1/b3
with the adopted mulligan profile live:

**ONE cleanup discard in 400 games**, and it shed **Doubling Season** -- the predicted failure,
observed. So the rule is wrong but almost never consulted: this is a correctness wart with a very
small expected value, and any fix must be judged on that basis rather than on how bad the ordering
looks. Fungus empties its hand; it does not flood past seven.

## 5. THE USER'S BUCKETING (ruling, 2026-09-24)

> *"Rather than just an order, the best way to deal with it is to start with buckets. One for mana,
> one for threats and perhaps one for enablers. (doubling season + beastmaster) Though you only
> really want 1 Doubling Season and 1 Beastmaster at the most. Mana we should aim to have the lesser
> of 3-4 spots and 5 total mana (counting the board) We should keep a mix of Fungus, but especially
> one Sporecrown, one Mycoloth, one Sporesower and a 1-drop, preferably Utopia Mycon."*

**A RETENTION TARGET PER BUCKET, not a shed ranking.** The shed list is then derived: whatever
exceeds its bucket's target is surplus, and surplus is what gets discarded. That inverts the base
rule's question from "which card is worst" to "which bucket is over quota", which is the right
question for a deck whose hand is a mix of interchangeable pieces.

| bucket | members | retention target |
|---|---|---|
| **MANA** | Forest, Simic Growth Chamber, Wild Growth | `min(3, max(0, 5 - board_mana))` lands + 1 accelerator |
| **ENABLERS** | Doubling Season, Beastmaster Ascension | **1 of each, at most** |
| **THREATS** | the Fungus creatures (+ Essence Warden) | a MIX, with a protected core |

* **MANA**, stated by the USER over three messages and reconciled here:
  *"the lesser of 3-4 spots and 5 total mana (counting the board)"*, then *"only 3 lands at most and
  maybe one accelerator. If we have any mana on board, probably keep it at 3"*, then
  *"(or whatever we need to reach 5 total mana)"*. Together that is

      lands_to_keep = min(3, max(0, 5 - board_mana))      accelerators_to_keep = 1

  -- a cap of 3 that TIGHTENS as the board fills, not a flat 3. **5 is the deck's top of curve**
  (Mycoloth `{3}{G}{G}`, Doubling Season `{4}{G}`), so once the board alone makes 5 the target is
  ZERO and every land in hand is surplus. Board at 1-2 mana is the common case at a cleanup
  discard, which is why "probably keep it at 3" is the usual answer. The 3 counts Forest and Simic
  Growth Chamber together; `board_mana` is mana the board makes in a turn, so a Chamber counts 2
  and each Wild Growth adds 1 (the engine already has `OptimisticTurnMana` for exactly this).
* **THREATS protected core:** one Sporecrown Thallid, one Mycoloth, one Sporesower Thallid, and one
  one-drop -- **preferably Utopia Mycon** (it is the free sac outlet: "Sacrifice a Saproling: add
  one mana of any color", which is the deck's real mana engine once a swarm exists).

Cross-bucket shed order follows from the targets: surplus MANA, then surplus ENABLERS (the 2nd+
Doubling Season / Beastmaster), then surplus THREATS beyond the core.

Adopt via the heuristic-optimization route (train seeds, then held-out), behind a `heurarm` slot,
like every other Fungus lever -- though see the frequency in section 4 before spending a sweep on it.

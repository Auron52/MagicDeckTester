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

## 4. The shape a fix would take

A `FungusProvider::CleanupDiscardCandidates` override. The deck's own logic is not subtle -- the
USER has already stated the board-side version of it for devour targets (quoted in
`fungus-token-search-cost.md` Round 9), and the hand-side analogue is:

1. excess LANDS first once the land count on board is sufficient (22 lands, nothing uses them as
   ammunition, so `DiscardLandsFirst` is the wrong lever -- this wants a "surplus land" test, not an
   unconditional land-first);
2. then redundant vanilla bodies (a 4th Thallid / Tukatongue) -- the same interchangeability the
   spore pool and devour narrowing already exploit;
3. NEVER Mycoloth, and never the first Doubling Season. A SECOND Doubling Season is close to dead
   (it doubles what the first already doubles, at 5 mana), so copy-aware ranking matters here --
   the redundancy idea `DiscardProtectScope` already encodes.

Adopt via the heuristic-optimization route (train seeds, then held-out), behind a `heurarm` slot,
like every other Fungus lever.

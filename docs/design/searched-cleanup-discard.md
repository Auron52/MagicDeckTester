# Searched cleanup discard (MTG_SEARCHED_DISCARD — ships OFF)

2026-07-21. Motivation (user): the cleanup discard heuristic is "often awful" — in Dragonstorm it
pitches **Apex of Power / Dragonstorm** (the only combo payoffs), leaving the deck stranded. The
correct discard is a **stray Dragon → redundant mana → redundant payoff** (Apex before Dragonstorm),
never the only payoff. "If you have mana for Dragonstorm, storm 3, and Dragonstorm itself, you're
set" — so a discard that keeps that assembled wins; one that pitches it loses.

## What was built

`AIEngine::ChooseDiscard` gained a **searched** path that mirrors the lookahead **bottomer**
(`BottomCards`): roll out a full clairvoyant game for discarding each candidate (the card goes to
the **graveyard**, unlike bottoming's library-bottom), keep only the discards that preserve the
**earliest win**, and let the existing highest-MV heuristic break ties. A discard that strands the
win rolls out to a later/no win and is excluded; a redundant/stray card preserves it. This is the
"searched, with heuristic fallback" the user asked for — the search finds "don't pitch the only
payoff" automatically (no per-deck rule).

### Recursion safety (the load-bearing guard)
`RolloutWinTurn` runs `GameEngine::PlayOut`, whose cleanup calls `ChooseDiscard` again. A naive
searched discard would recurse exponentially. Guard: `!m_in_rollout` (set true for the duration of
`RolloutWinTurn`). The searched pass fires **only at the top-level real decision**; every rollout's
own cleanup uses the heuristic — so rollout labels are unchanged and only the REAL discard is
refined. Also gated on `LookaheadBottoming()` (depth > 0); d0 is inert.

## Why it ships OFF (`MTG_SEARCHED_DISCARD`, default unset = heuristic = byte-identical)

Measured (smoke seed 1001, full suite): **neutral on every deck except Treasure Hunt, which got
slightly WORSE** — th d3 4.2333→4.2400 (+0.007), th d5 4.1600→4.1733 (+0.013). No deck improved;
Dragonstorm was byte-identical (its combo-discard case is absent from seed 1001). It also costs real
compute (hand_size full-game rollouts per real cleanup discard).

Two reasons it can regress:
1. **Train/serve mismatch:** each candidate rollout assumes the HEURISTIC for its *later* discards
   (m_in_rollout), but the real game will SEARCH those later discards → the rollout's win-turn
   ranking can mis-order candidates vs. what actually realizes.
2. **Clairvoyance:** `RolloutWinTurn` sees the true future draws, so it optimizes the discard for one
   drawn future; under the non-clairvoyant realized game that edge can invert.

Adopting a measured regression violates the heuristic-optimization discipline, so it ships behind the
flag, default off, pending a **reproduced combo-discard win**.

## Path forward
- Reproduce the Dragonstorm case where the heuristic pitches Apex/Dragonstorm (the user saw it in a
  seed-6/7 game; it isn't in seed 1001). Confirm `MTG_SEARCHED_DISCARD=1` fixes it and by how much.
- If the combo-win benefit outweighs the TH cost across regression/overnight seeds, adopt (flip the
  default, rebaseline GT). Otherwise keep OFF.
- Cheaper alternative to weigh: improve the *heuristic* itself (used in rollouts AND as fallback) so
  it never pitches the only copy of a high-value spell — e.g. rank by the deck's `card_scores`
  (keep-value) rather than raw MV, protecting unique payoffs while shedding redundant copies / excess
  mana. Deterministic, no rollout cost, and it also improves the rollout labels the search learns
  from. This may capture most of the benefit without the train/serve/clairvoyance downsides.

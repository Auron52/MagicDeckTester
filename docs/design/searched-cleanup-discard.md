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

## 2026-07-28 update — the reproduction exists, and the heuristic route is now exhausted

**The combo-discard case this doc was waiting for has been found and is committed as ground truth:**
overnight seed 7007, games `gi79` / `gi193` / `gi379` (d3 and d5). All three keep an IDENTICAL hand
and draws, so they are clean like-for-like line changes, and in each the heuristic sheds the
rituals that are the deck's mana — casting 0–5 spells instead of 6–8 and turning wins on T8/T6/T5
into unwon. They are unreachable by more search: all three stay unwon at **depth 8 / 20 000 ms**,
because the discard is not a searched decision at all.

`MTG_SEARCHED_DISCARD=1` recovers **all three**, including `gi79`, which no heuristic scope can
reach (see below). That is the "reproduced combo-discard win" this doc set as the adoption
precondition.

The cheaper heuristic alternative (last bullet of the old path forward) has since been taken as far
as it goes, in two steps, and it is now provably short of the searched result:

1. `a12b753` protected `required_pieces` from the cleanup discard. This is what CAUSED the three
   games above — protecting *every* copy leaves the rituals as the highest-MV shed.
2. `mulligan.discard_protect` (this session) made the protection **per-deck and last-copy-only**.
   Measured over the overnight suite: dragonstorm −0.0128 searched / −0.0265 d0, antilife −0.001,
   th and burn untouched (they keep `all`; th genuinely prefers protecting duplicates), every deck
   with empty `required_pieces` byte-identical. It recovers `gi193` and `gi379`.

`gi79` is the residual and it shows the ceiling of the static approach: the hand holds ONE Apex and
ONE Dragonstorm — both last-in-hand, so both protected — while the LIBRARY still holds more copies,
and the deck is mana-screwed. The correct shed depends on board state (screwed vs not), which a
static per-deck constant cannot express. A `deck`-scope variant (protect only when no copy remains
in hand or library) was measured too: it reproduces the pre-`a12b753` ground truth EXACTLY on all 12
overnight cases, i.e. it never fires and is a silent full revert. So the three scopes bracket the
whole static design space, and none of them gets `gi79`.

### Remaining path forward
- A/B `MTG_SEARCHED_DISCARD=1` across regression + overnight **against the new per-deck-heuristic
  baseline** (not the old one the 2026-07-21 measurement used — that baseline no longer exists).
  The old TH cost (+0.007 d3 / +0.013 d5 on smoke seed 1001) is the number to re-check first, since
  TH is the deck that prefers the protective behaviour.
- Note the searched path uses the heuristic for its *rollouts'* own discards (`m_in_rollout`), so
  the improved per-deck heuristic now also improves the labels the search ranks on — the two changes
  compose rather than compete, and the train/serve mismatch of reason 1 above is smaller than when
  it was first measured.
- If it wins, flip the default and rebaseline; the `discard_protect` field then becomes the
  rollout/fallback policy only, and should be re-measured (its per-deck values were tuned against a
  heuristic-only engine).

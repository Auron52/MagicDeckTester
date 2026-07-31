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

---

## 2026-07-31 — the TH cost was a BUG in the trial state, not a property of searching

The A/B above was run against the current baseline and reproduced the old result almost exactly:
**th d3 +0.0334, th d5 +0.0533** on smoke seed 1001, dragonstorm d3 −0.0066 — net clearly worse. But
the per-game audit pointed at one game, `th_smoke_d3_s1001 gi61`, which fell from a **T4 win to T8**,
and that game turned out to be a defect in how each candidate was *evaluated*, not evidence about
searching the discard.

### The defect

`RolloutWinTurn` calls `GameEngine::PlayOut`, and `PlayOut` **always starts a fresh turn** —
`RunTurn` opens with `++state.turn_number`. A trial state captured *in the middle of the cleanup
step* is therefore never resumed at cleanup: the remaining sheds simply never happen, and the rollout
plays the next turn holding the entire untrimmed hand.

That is not a small distortion on the deck where the discard matters most. Treasure Hunt's payoff is
**Land's Edge**, which converts each land discarded into 2 damage, so an 18-card hand carried into
the next turn is ~20 points of phantom ammunition. The trace shows exactly that: at the turn-2
cleanup of gi61 (hand 18, eleven cards to shed) **every** candidate scored a phantom `win=3`, and at
the 8th shed the ranking finally separated on phantom damage and picked `Treasure Hunt` (win=3) over
the heuristic's `Fiery Islet` (win=4). The realised game needed that third Treasure Hunt on T4 to
refill for the actual Land's Edge kill: T4 → T8.

So the searched pass was ranking states the real game can never reach, and the deck that suffered
was the one whose cards turn hand size into damage.

### The fix

`FinishCleanupTrial` (AIEngine.cpp): after applying the candidate shed, finish the cleanup with the
provider heuristic before rolling out. The searched candidate is then only the **first** shed of that
cleanup — the rest follow the default — which is the same "branch on the first divergence, heuristic
for the tail" shape the plan search's breakpoints use, and it makes every label a legal state.

### Measured after the fix (searched discard ON vs the current baseline)

| mode | result |
|---|---|
| smoke (1001) | th d3 and th d5 back to **byte-identical**; dragonstorm d3 −0.0066 (gi112 5→4). **1 changed case, 1 game faster, 0 slower** |
| regression (2002, 3003) | 9 changed cases, **15 games faster, 0 slower, 0 play-changed**: th −0.0040/−0.0040/−0.0034, antilife −0.0066/−0.0080, hinata −0.0050/−0.0150/−0.0100/−0.0100 |

Both TH regressions this doc had flagged as the blocker were the bug, not the feature. With the trial
state legal, searching the discard is monotone-better on every case that moved, on both seed sets.
d0 is untouched (the pass is gated on `LookaheadBottoming()`).

`MTG_DISCARD_TRACE=1` prints the per-candidate win turns and the heuristic pick for every real
cleanup discard — that trace is what isolated this, and it is kept for the next one.

## The second defect: a committed line that assumed the heuristic shed

With legal trial states the held-out overnight arm came out **30 games faster / 4 slower**
(net −0.0428), and the 4 slower ones were worth the read. One (`th d5 s6006 gi232`) was budget churn
— it recovers at 4x. The other three (`s5005 gi53` 5→6, `s5005 gi713` 6→8, `s6006 gi833` 6→7)
persisted at **16x** budget, and all three had the same signature: the searched pass shed **Throes of
Chaos** where the heuristic shed a land.

The shed was not the problem. The problem was what happened next:

* Every plan sitting in `m_committed_line` was searched through
  `SimulateEndAndStartNextTurn`, whose cleanup sheds the **heuristic** card. The line therefore
  assumes a hand that a deviating discard has just falsified.
* The rollout that justified the deviation assumed **no line at all** — `RolloutWinTurnFrom` stashes
  the real line and runs the trial on an empty one, so its continuation re-searches every turn.

So the label came from a fresh search and the realised game came from a stale plan: a train/serve
split created by the decision itself. `MTG_DISCARD_RELINE` (default **on**) drops the committed line
whenever the searched discard deviates, so the next turn re-searches the state the discard actually
produced.

| game (d5) | heuristic | searched, stale line | searched + re-line |
|---|---|---|---|
| s5005 gi53 | 5 | 6 | **4** |
| s5005 gi713 | 6 | 8 | **5** |
| s6006 gi833 | 6 | 7 | **5** |
| s6006 gi232 | 7 | unwon | **6** |

All four were better than the heuristic once the line was invalidated — the discards were right and
the replay was wrong. Note this is a general hazard, not a discard-specific one: **any** executor-side
decision that deviates from what the search assumed leaves a stale committed line behind it.

## ADOPTED 2026-07-31 — default ON

With both defects fixed the feature is monotone-better on every seed set, and no game anywhere gets
slower:

| mode | changed cases | per-game | summed avg-win-turn delta |
|---|---|---|---|
| smoke (1001) | 1 | 1 faster, 0 slower | −0.0066 |
| regression (2002, 3003) | 9 | 15 faster, 0 slower, 0 play-changed | −0.0660 |
| overnight, HELD OUT (4004–7007) | 19 | **39 faster, 0 slower**, 2 play-changed | −0.0658 |

Per deck on the held-out seeds: hinata −0.0325, th −0.0180, antilife −0.0080, dragonstorm −0.0073.
Depth 0 is byte-identical everywhere (the pass is gated on `LookaheadBottoming()`), and every deck
whose cleanup never fires stays byte-identical.

The two play-changed games (`th d3 s5005 gi161`, `th d3 s6006 gi324`) are the same benign shape: the
searched pass sheds a redundant **Land's Edge** at the turn-2 cleanup and the game still wins on T6
by a different route (a later Land's Edge, or Throes of Chaos). Worth noting what they show — the
rollout ranked those sheds strictly better and the realised game came out merely equal, so label
noise has not gone away; it is just no longer producing losses.

`discard_protect` (per-deck, last-copy-only) now serves as the **prune and tie-break** for the search
rather than the whole decision. Its per-deck values were tuned against a heuristic-only engine and
are due a re-measurement in that new role.

### What is still heuristic here
* Sheds 2..N of the same cleanup are each searched independently, with the heuristic assumed for the
  tail — coordinate descent over what is really a "which 7 do I keep" set decision.
* Rollouts inside the search still use the heuristic for their own discards (`m_in_rollout`), which
  is what keeps the cost bounded.

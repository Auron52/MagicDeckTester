# Cost-neutral action sub-decisions were never searched — the dedup ate them

2026-07-31. Found while root-causing why the plain-cantrip breakpoint class cannot afford itself
(`hinata-branching-root-cause.md`). The answer turned out to be bigger than the cantrip class.

## The bug

`EnumeratePlans` finishes by deduplicating its plans on `plan_signature`, and **autonomously that
signature keys only on cast NAMES**. Every per-`Action` sub-decision is in a block gated on
`HumanPlayActive()`:

```cpp
if (s_human_play_sig)
{
    if (!act.tutor_target.empty())    { sub.push_back("t" + ...); }
    if (act.chosen_x > 0)             { sub.push_back("x" + ...); }
    if (act.ponder_keep >= 0)         { sub.push_back("p" + ...); }
    if (act.soulfire_own_targets > 0) { sub.push_back("f" + ...); }
    if (act.splice_count > 0)         { sub.push_back("k" + ...); }
    if (act.enchant_target > 0)       { sub.push_back("e" + ...); }
}
```

So for the autonomous search, two plans that cast the same cards but make different sub-decisions
have the **same signature**, and the dedup keeps the first-enumerated — which is the provider's
best-first pick. The code says this outright, and has for a long time:

> "The autonomous dedup collapses them to one cast-name representative (the FIRST enumerated, i.e.
> the tutor heuristic's best-first pick). **NB this is NOT a correctness property -- it is an
> efficiency shortcut that DELEGATES the sub-decision to the heuristic and never search-branches
> over the alternatives.**"

Two consequences, and the second is the surprising one:

1. **The decision is not searched.** It is the provider's ordering wearing a search's clothes.
   A `tutor_target` and a `ponder_keep` cannot change what a plan can afford, so their variants
   *always* share a signature and are *always* collapsed — 100% of the time, for every deck.
   (Cost-*changing* sub-decisions — `chosen_x`, `splice_count`, `soulfire_own_targets` — survive
   only when the different cost changes which other cards are affordable, i.e. incidentally, never
   on their own merits.)
2. **The work was done anyway and then thrown away.** The variants are emitted inside
   `CollectActions`, so they are full option-group members and multiply the plan odometer, and only
   then get discarded. On Hinata this was the single largest cost in the engine.

## Measured (Hinata, seed 4004, d3, budget 10, `MTG_BRANCH_STATS`)

Gamble is `tutor_to_hand` with no `tutor_types` filter, so once Hinata is online
`HinataProvider::TutorCandidates` deliberately returns *every distinct library name* — ~30 options
in a deck with 34 distinct names. That group is one **factor** of the odometer, so it multiplied
the whole rest of the turn:

| | before | after (emit 1, axis for the rest) |
|---|---|---|
| Gamble share of total odometer | **75%** (2 845 016 of 3 792 728) | — |
| sum odometer (10-game probe) | 1 239 735 | **538 503** (−57%) |
| sum raw plans | 534 724 | 158 729 (−70%) |
| `sum_final` (post-dedup plans) | 62 952 | 62 952 (identical) |
| play | — | **byte-identical** |

`sum_final` and the play being identical is the proof that all of that work was waste: capping
emission to one candidate changes nothing downstream, because the dedup was already keeping exactly
that one.

**Caveat, measured — the emission cap is near-neutral, NOT byte-identical.** The equivalence above
holds for `EnumeratePlans` (which dedups), but `TurnSolver::Solve` has its **own** odometer and no
signature dedup, and it is the d0 decision *and* every rollout leaf. There, removing candidates can
change which of several tied plans wins (its tie-break is smallest odometer mask), so the cap is a
small play churn. Measured on the full held-out suite at axis width 1: **9 of 108 cases changed,
searched-depth sum 0.0000 (0 slower / 0 faster, pure digest churn), d0 sum −0.0015.** A wash, not a
regression — but the doc originally claimed byte-identical and that was wrong. Ruled out as the
cause: `MTG_NO_GROUP_CAP=1` reproduces the same difference, so it is not the breadth cap.

### It also corrects an earlier reading in this repo

`searched-scry-disposition.md` records that the Ponder keep-vs-shuffle branch measured 0.325 *worse*
with the pinned-keep variant enumerated first, and became free once the heuristic was enumerated
first — attributed there to the search's strict-improvement tie-break. The tie-break argument is
sound in general, but it is **not** what happened here. Ponder's three variants share a cast-name
set, so the dedup keeps only the first: enumerating keep first made the engine *always keep* (hence
0 shuffles in 48 resolutions), and enumerating the heuristic first made the branch **not exist**
rather than free. Confirmed directly: `MTG_PONDER_SEARCH=0` and `=1` produce identical play
(Hinata, 200 games, d3 b10 — 5.8950 both). The fix that shipped was still the right one; the
mechanism was misdiagnosed, and the decision is still unsearched.

## The fix: emit one, fan out after the dedup

This is the shape the engine already uses for every other inline sub-decision — `fetch_target`,
`land_face`, `scry_choice`, `bp_choice`: put one option in the odometer, then emit variants
**after** `EnumeratePlans` returns, in `EnumeratePlansWithLand`. Cost becomes `P + W` instead of
`P × k`, and the alternatives actually get scored.

`MTG_TUTOR_AXIS=0` restores the old collapse; `MTG_TUTOR_WIDTH=<n>` overrides how many targets the
axis scores including the provider's best (so `n=1` is exactly the old behaviour — verified
byte-identical). Unset, the width comes from the **provider** (`DecisionProvider::TutorSearchWidth`).

### The width is per-deck, and the per-deck optima genuinely diverge

The first sweep produced one global constant, 6. That was the best available *average*, but the
per-deck curves it averaged over do not have the same shape at all — they have three different
shapes, and the mechanism for each is legible:

| deck | width | curve | why |
|------|-------|-------|-----|
| antilife | **2** | flat from 2 | the pool *is* 2 — the deck's only enchantments are Tainted Remedy and Aria of Flame and it runs no artifacts, so both tutors see the same two-card list. Play digests are byte-identical from width 2 to 12: there was never a third candidate. |
| hinata | 6 (base, **not** overridden) | peaks at 2 on train, then dilutes | `TutorCandidates` is already combo-aware (Hinata alone while she is missing; `SituationalCardRank` order once she is online), so the hypothesis was that extra targets only dilute. Train agreed; **the holdout did not** — see below. |
| goblins | **12** | monotone, knee at 8 | Matron fetches "a Goblin card" from ~16 distinct names that are *not* close substitutes (Muxus vs. Mogg War Marshal), so the search keeps finding value well past the top few. |

Measured on the TRAIN seeds (1001/2002/3003), searched-depth sum vs width 1:

```
width      antilife       goblins        hinata
    1       +0.0000       +0.0000       +0.0000
    2       -0.0293       -0.0294       -0.0568   <- antilife & hinata best
    4       -0.0293       -0.1627       -0.0518
    6       -0.0293       -0.1846       -0.0284   <- the old global default
    8       -0.0293       -0.2100       -0.0484
   12       -0.0293       -0.2108       -0.0168   <- goblins best
```

Two things this makes concrete. First, a single width is not a compromise between these — at 6 it
was paying antilife's enumeration for a provably empty candidate list to buy goblins a gain it
would rather have had in full. Second, **width is not monotonically good**: the tutor axis is
additive in cost, but the budget it consumes is not free, so a deck whose provider ranks well is
not helped by searching deeper. That is the same result the Ponder ORDER axis produced from the
other direction.

### What the holdout was for: one of the three picks did not survive it

The widths were chosen on the TRAIN seeds specifically so the overnight seeds stayed a validation
set (`test/tutor_width_sweep.sh` sweeps; `test/tutor_width_holdout.sh` confirms **once**). Running
the finished config against the shipped global 6 on the held-out seeds:

```
=== SEARCHED (d3/d5) ===          delta vs global width 6
  goblins    -0.0620    8 of 8 cases improve, none worse
  hinata     +0.0009    a wash -- the train delta did NOT replicate
  antilife    0.0000    exactly as predicted: identical play, less enumeration
  d0          0.0000    every deck, as expected -- no rollout to score a variant with
```

Goblins and antilife shipped. **Hinata's did not**: train said width 2 beat 6 by −0.0284, the
direct held-out comparison said +0.0009, and width 2 is not cheaper either (overnight makespan 68s
vs 69s), so there is no cost argument to fall back on. A train-only delta of that size, on the arm
that was *selected because* it was best on train, is what a selection artifact looks like — so
Hinata keeps the base 6. This is the split doing its job; without it, "two of three decks want 2"
would have read as a pattern.

The **base** class default stays 6, deliberately untouched: it now only governs decks with no
measurement behind them, and re-tuning it off these three would be fitting a default to the decks
that no longer use it.

### Why a second axis loses nothing *this* turn for a tutor

A tutor is **not** a breakpoint site (the five are stages/EI, `DrawUntilNonland`, `impulse_exile`,
plain cantrip, dig-through-lands) and Gamble is not a draw spell, so no re-solve follows the fetch:
the plan's action list is frozen before the fetched card arrives, and **it cannot be cast this
turn.** The target therefore cannot interact with the rest of this turn's subset — which is exactly
the condition under which a second axis is *equivalent* to the cross product rather than an
approximation of it. (The one residual interaction, Gamble's seeded `discard_random_after_tutor`,
picks a hand index that is the same whichever card was fetched, since the fetch appends.)

That is a dominance argument, not a guess, which is what makes this different from narrowing the
candidate set with a heuristic.

## Still open

`ponder_keep` is DONE (`083b546`, −0.8467 over 7 seeds, and faster) — see the commit and the
memory note; its ORDER sub-axis was measured and **rejected** at +0.0266. The cost-changing
sub-decisions (`chosen_x`, `splice_count`, `soulfire_own_targets`, `enchant_target`) are a separate
and harder case: they are only *partly* collapsed, and moving them out of the odometer would change
what the mana solve can see.

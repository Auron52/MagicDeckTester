# The order-free reuse wave — recovering split keys' residual without reintroducing the bug

**Status: ADOPTED, default ON since 2026-09-10 (`202f938b`), all three GT tiers rebaselined,
CI green including Linux/Windows determinism parity.** Flags: `MTG_FSL_OF_WAVE` (on/off, `=0` for the
A/B), `MTG_OF_WAVE_SHARE` (the gate), plus per-job `of_wave` / `of_wave_share` arms so a sweep runs as
ONE pooled batch. Probe: `MTG_OF_WAVE_PROBE` (counters only, no behaviour).

Background for everything below: `draw-divergence-diagnosis.md` (how the unsound shortcut was found,
and the USER rulings that bound what may replace it).

## What was given up, and how much it was worth

Split keys (`MTG_FSL_SPLIT_KEYS`, default ON since 2026-09-10) made the transposition memo sound *by
construction*: WIN entries go under an ORDER-EXACT key, NO-WIN entries stay canonical. Nothing is
ever reused across a zone permutation, so the unsoundness cannot occur.

It paid for that. A state differing from an entry's author only in vector order no longer finds the
WIN and re-searches the position from scratch. Measured on 40 Hinata games at d5/b20 with
`MTG_OF_WAVE_PROBE=1`:

```
re_searches=46695  twin_win_available=14208  avail_share=0.304
```

**30.4% of all re-searches had a permuted twin's answer sitting unused.** That is the whole residual
the sound adoption still owed: +0.0990 t against the unsound baseline on the regression tier.

## The measurement that decided the design

The obvious move — take the twin's answer — is exactly what the old unsound shortcut did. So the
question is not "is it fast" but "how often is it RIGHT". The twin-agreement probe answers it
directly: let the node compute its own answer, then compare with the twin's.

```
twin_agree=14165  twin_better=23  twin_worse=20     (agree 99.70%, better 0.16%, worse 0.14%)
```

Read this carefully, because it explains every earlier observation at once:

* The shortcut was **right 99.70% of the time**, which is why it was cheap and mostly harmless, and
  why the original evidence for it ("lost 3 of 16000 Fluctuator games") looked like an acceptable price.
* The **23 `twin_better` nodes are the damage**: the node's own search found a STRICTLY EARLIER win
  than the answer it would have been handed. That is the hinata gi232 shape exactly — take the handed
  answer and a real win is thrown away.
* There is **no way to tell a good hit from one of the 23 without searching**. So no confidence
  threshold, no verification predicate, and no "verified-only" filter can separate them. Any static
  rule either takes all of them or none.

Two shapes were built and measured before landing on the gate, and both are recorded here so they are
not re-tried:

* **Move-order hint** (take only WHICH PLAY opened the twin's win; rotate it to the front of the
  candidate list). Believes nothing, skips nothing, sound at every budget — and worth nothing: of
  14208 twin hits it reached a candidate scan 65 times and the hinted play was **already first in 59
  of those (91%)**, for 6 actual rotations. `MoveOrderPlans` is already picking it.
* **Replay the twin's line as an honest incumbent.** Sound (`ApplyPlanDirect` resolves every action
  by card NAME and the canonical key guarantees the same multiset, so a line IS replayable under a
  permutation), but it cannot pay: the node would still search, and its own plan 0 already yields an
  equal-or-better incumbent. The value in a twin's answer is the WORK IT SAVES, not the order or the
  bound it suggests.

## The mechanism: a budget gate, not a rule

A node may take its twin's answer only when re-searching would cost a serious share of what is LEFT
to spend:

```
twin_units > share x budget->Remaining()          // SatMulD, so unlimited needs no branch
```

`twin_units` is the subtree-inclusive work the twin's node actually consumed, measured and stored
beside its answer — not a guess at one. The shape follows `SearchBudget.h`'s own standing rule,
*"UNLIMITED IS NOT A SPECIAL CASE -- it is the limit that increasing budgets converge toward"*.

What that does at the ends of the scale:

* `Remaining()` grows with the budget, so the condition fails more and more often. The reuse rate
  decays **continuously** to zero — no cliff, and no "large-but-finite behaves like tiny" step (the
  failure mode that got `Unlimited()`-only wave gating rejected in 2026-07-29).
* At an unlimited budget `Remaining()` is `LLONG_MAX`, so the gate **never opens** and the search is
  exactly the sound one.
* Within one budget it drains as the search proceeds: cheap early, thrifty late. The ordinary anytime
  contract.

**This is not offered as a proof.** A reuse still believes an estimate computed under another zone
order, so it is booked as what it is — a **budget-starvation fallback**, the one carve-out the
standing bar allows (*"Anything that stops search should be eliminated. The only cases where this
should be able to happen is when you are budget starved"*). Every reuse bumps `g_fs_trunc_events`, so
no refutation resting on one is ever cached as a real one, and a committed line that ends at a reuse
is re-searched with the shortcut off (`g_orderfree_off`) before it is ever played.

## The acceptance test, run on the game that started this

USER's bar: *"it only working at unlimited is also not acceptable, but it may potentially require a
high budget"* — i.e. **unreachable at ANY budget is a BUG; reachable-with-more-budget is acceptable
CHURN.** The old shortcut failed it outright (hinata gi232 was 6 at every depth 5..40 with the budget
UNLIMITED). `hinata overnight d5 s6006 gi232`, sound baseline = 5:

| share | b20 | b320 | unlimited |
|---|---|---|---|
| 1e-5  | win 5, reused 294/294 | win 5, reused 389/389 | win 5, **reused 0** |
| 0.002 | win 5, reused 217/263 | win 5, reused 5/262    | win 5, **reused 0** |
| 0.05  | win 5, reused 56/176  | win 5, reused 0/316    | win 5, **reused 0** |

The turn-5 kill survives in every configuration, and `reused=0` at every share once the budget is
unlimited: the gate provably closes. The middle column is the point of the whole design — the
constraint lifts *continuously*, not at a cliff.

## Sizing the share (200 Hinata games, d5/b20, deterministic units)

| share | avg turn-to-win | units | vs control |
|---|---|---|---|
| control (wave off) | 5.8050 | 5,756,393 | 1.000x |
| 0.0005 | **5.7950** | 4,697,067 | 0.816x |
| 0.001  | **5.7950** | 4,704,262 | 0.817x |
| 0.002  | **5.7950** | 4,792,034 | 0.832x |
| 0.003  | **5.7950** | 4,813,365 | 0.836x |
| 0.005  | **5.7950** | 4,874,265 | 0.847x |
| 0.01   | 5.8000 | 4,994,895 | 0.868x |

Better AND cheaper: -0.0100 t at 0.82-0.85x the work. (Cheaper because the budget is a ceiling, not a
quota — answering more transpositions from the memo lets the ladder's passes complete for less.)

**Chosen: `share = 0.005`, the LARGEST value still on the quality plateau.** The plateau is flat in
quality, so the tie-break is convergence: a larger share closes the gate sooner as the budget grows,
which means a high-budget run gets more of the exact, sound search. Paying ~3% more units at the
shipped budget to buy that is the right side of *"how this scales should always be done based on
using our budget as well as possible"*.

### ...AND THAT SIZING WAS UNDERPOWERED. Re-done at 40,000 games (2026-09-11)

**The table above is not a plateau, it is a sample too small to see the effect.** Every share <= 0.005
reads 5.7950 because 200 games of one deck CANNOT resolve a difference that lives at roughly one game
in a thousand — the same blindness that made the per-deck soundness cost look concentrated in 4 decks
when it is spread across most of them. Re-sized on 20 decks x 2,000 games, paired against the UNSOUND
arm on identical seeds (seed base 200000, b20, `test/paired_arms.py`):

| share | delta vs unsound | +/- se | better | worse | sign p | units |
|---|---|---|---|---|---|---|
| 0.0002 | -0.0000 | 0.0001 | 6 | 5 | 1.000 | 0.956x |
| 0.0005 | -0.0001 | 0.0001 | 7 | 5 | 0.774 | 0.959x |
| 0.001 | +0.0001 | 0.0001 | 6 | 8 | 0.791 | 0.966x |
| **0.002** | **+0.0002** | 0.0001 | 9 | 15 | **0.307** | 0.975x |
| 0.005 (shipped) | +0.0008 | 0.0002 | 7 | 36 | 0.000 | 0.982x |
| 0.01 | +0.0011 | 0.0002 | 8 | 48 | 0.000 | 0.980x |
| 0.02 | +0.0014 | 0.0002 | 9 | 61 | 0.000 | 0.986x |
| 0.05 | +0.0016 | 0.0003 | 13 | 72 | 0.000 | 1.000x |
| 0.10 | +0.0016 | 0.0003 | 14 | 74 | 0.000 | 1.009x |
| wave off (`split`) | +0.0019 | 0.0003 | 16 | 84 | 0.000 | 1.290x |

Monotone, and it lands the whole ladder: **the soundness cost falls to ZERO around share 0.0005-0.002**,
where the sound engine is indistinguishable from the unsound one (p >= 0.31) at ~2-4% LESS work.
Head-to-head on the same 40,000 games, `0.002` beats `0.005` **24 games better / 1 worse** (p<0.001,
-0.0006 t, 0.99x units).

**The tie-break rule was right; it was applied to a plateau that was an artifact.** Re-applying it to
the real curve: the no-measurable-cost plateau spans 0.0002-0.002, and the LARGEST value on it is
**0.002**, so that is the candidate. Going lower buys nothing measurable in quality and only defers
soundness to a higher budget (the gate closes at budget proportional to `1/share`).

**Lowering the share does NOT reintroduce the failure that started all this.** `hinata gi232` (seed
6006+232, the game the old unsound reuse made unreachable at ANY budget) returns the sound turn-5 kill
at EVERY share in 0.0002..0.005 and EVERY budget in b20/b80/b320/b1280/b5120/unlimited — byte-identical
digests down the whole grid. That is the acceptance test, and share does not move it.

### Held-out confirmation of the re-sizing, and the candidate: `share = 0.001`

The curve above is fitted on seed base 200000, so it was re-measured on a fresh **seed base 300000**
(20 decks x 2,000 games, arms `unsound` / 0.005 / 0.002 / 0.001). Pooled over BOTH bases = 80,000
paired games per arm:

| share | delta vs unsound | sign p | units | head-to-head vs 0.005 |
|---|---|---|---|---|
| **0.001** | **+0.0001** | **0.442** | 0.923x | **-0.0005 [-0.0007,-0.0002], 54 better / 19 worse, p<0.001** |
| 0.002 | +0.0002 | 0.049 | 0.934x | -0.0003 [-0.0005,-0.0001], 35 / 10, p<0.001 |
| 0.005 (shipped) | +0.0005 | 0.000 | 0.940x | — |

**One result did NOT replicate, and it is the one the training base shouted loudest about.** On seed
base 200000, `0.002` beat `0.005` **24 games better / 1 worse**; held out it is **11 / 9, p=0.82 — a
wash**. Per deck the training win was mostly hinata (8/0) and melira (5/0) with zero losses anywhere;
held out, fivecolour goes 2/6 the other way and cancels it. Treat any single-base head-to-head at this
effect size as provisional.

`0.001` holds direction on both bases independently (train -0.0007, 32/5; held out -0.0002, 22/14) and
is decisive pooled. **The pick is therefore `0.001`: the LARGEST share still indistinguishable from the
unsound engine** (p=0.44 vs 0.049 at 0.002), which is the original convergence tie-break applied to a
curve that now has enough data to have a shape.

### Every residual loss is CHURN — 36 of 36 recover with budget

The acceptance bar is *"unreachable at ANY budget = BUG; reachable but needing MORE budget =
acceptable CHURN"*, so each of the 36 games the sound engine loses to the unsound one at b20 was
laddered over b20/b80/b320/b1280/b5120 at BOTH shares (360 single-game jobs, pooled):

**36 of 36 recover, at both 0.005 and 0.001. Zero games stuck at every budget.** The three that no
share repairs at b20 — `kitty gi=0`, `mirrorwing gi=1156` (the one outright LOSS: won turn 8 unsound,
no win at b20), `mirrorwing gi=1760` — come back at b320, b320 and b80 respectively. `creature_giving
gi=818` overshoots, going 7 -> 5 at b80 where the unsound engine gets 7.

That is the whole soundness argument closed empirically: the sound engine gives up nothing that more
search cannot recover, which is exactly what the unsound memo could NOT say (gi232 was unreachable at
d5..d40 with unlimited budget).

**Verified against the rebased tree.** Upstream landed `escalation_r 74 -> 240` for Goblins plus
`MTG_ESC_DECK_R` / `MTG_ESC_SINGLE_DIAG` in `TurnSolver.cpp` mid-measurement. Rebuilt at the rebased
HEAD and re-ran 20 decks x 200 games x 3 arms: **60 of 60 arms byte-identical, per-game digests
included** (Goblins too — confirming their "identical play" claim). The numbers above stand at HEAD.

**Why lowering the share is not a soundness concession.** The gate is `twin_units > share x Remaining()`.
For ANY share > 0, `Remaining() -> inf` drives the threshold to infinity, so reuse vanishes and the
search converges on the exact answer. Share sets HOW FAST that happens, not WHETHER. Only `share = 0`
would break it (threshold pinned at 0 => reuse always), which is why the flag must stay strictly
positive.

## Why this cannot introduce nondeterminism

Worth stating explicitly, because a mechanism that reads the memo and changes what the search returns
is exactly the shape that usually does. `FSLineCache line_cache;` is a **local in `FullSearchLine`** —
built fresh for every decision, never shared between games and never touched by a second thread. So
the wave's index has the same lifetime, `twin_units` comes from `budget->Used()` deltas (deterministic
work units, never the clock), and `Remaining()` is likewise unit-denominated. Confirmed empirically:
identical digests across separate batches, the suite's `--strict` reference reproducibility clean on
the gating axes (`0 play-drift, 0 enum-gap, 0 mull-drift, 0 contract-fail`), and CI's Linux/Windows
determinism-parity job green on the adopting commit.

## Held-out confirmation

The share was sized on Hinata at seed 1001 and confirmed across 20 decks at the same seed, so the
sizing and its confirmation shared a seed. Re-measured on **seed 4004**, held out from both that and
the regression tier's 2002/3003, 120 games per deck, one pooled batch:

| | |
|---|---|
| quality | better 4 / worse 1 / identical 15, **NET -0.0335 t** |
| cost | **0.7852x** deterministic units |

The single worse deck is melira at +0.0083 (one game in 120). The adoption reproduces on seeds it was
never tuned against.

## THE NUMBER THAT MATTERS: what soundness actually costs, measured against the UNSOUND engine

USER, 2026-09-10: *"we should be minimizing the soundness cost and to be honest, it should be
extremely minor since we want the best result at our given budget. If we are leaving a lot on the
table that means we didn't do this well."* Right, and that is the bar this table is against. The whole
ladder, ONE pooled batch, 20 decks x 120 games at seed 4004 / b20 (2,400 games per arm), deterministic
units:

| arm | mean avg-turn | vs unsound | units |
|---|---|---|---|
| `unsound` (legacy reuse, split keys off, guard off) | 4.4208 | — | 1.000x |
| `guard` (verified-only only) | 4.4250 | +0.0042 | 1.563x |
| `split` (split keys, no wave) | 4.4242 | +0.0033 | 1.243x |
| **`shipped` (split keys + wave)** | **4.4225** | **+0.0017** | **0.976x** |

**The soundness cost is +0.0017 avg turns — 0.04% of one turn.** 16 of 20 decks are bit-identical to
the unsound arm; the residual is 4 decks at roughly one game each (stompy +0.0166, minotaur +0.0084,
hinata +0.0083, th +0.0083) against fivecolour -0.0084, i.e. about 4 game-turns out of 2,400 games.

Read the ladder left to right and it is the story of the whole arc: the naive guard cost +0.0042 at
**1.56x** the work, split keys took it to +0.0033 at 1.24x, and the wave takes it to +0.0017 at 0.976x.

**THE UNITS COLUMN IS TOTAL WORK, AND THE TOTAL IS TWO DECKS.** "The sound engine is cheaper than the
unsound one" is TRUE units-weighted (0.976x) and MISLEADING unweighted: fivecolour and melira dominate
the sum, and the MEDIAN deck pays **+12%** for soundness (per-deck mean 1.120x, median 1.116x, worst
1.424x on critter). Say both. The wave's OWN cost win survives either normalization -- 0.785x weighted,
0.833x per-deck mean, no deck above 1.000x -- which is why the wave is the part that pays for itself.

**BEWARE THE NORMALIZATION, because I got this wrong reporting it TWICE (once each way).** The suite's
"NET" is a SUM of per-key deltas over the keys that MOVED; quoting it without the denominator
(`+0.0688` over 11 moved keys of 108) makes the cost look material when the per-deck mean is +0.0017.
Same data, and the sum over these 20 decks is +0.0332. Then the units column above, where the
aggregate flattered the change instead. Always say which one you mean.

## READ THE LADDER PAIRED -- the mean is a summary, not the test

`test/paired_arms.py <wins_dir> --base unsound --arm shipped` compares the two arms GAME BY GAME (both
arms play the same seeds, so it is a paired design). On the 2,400-game run above:

**6 of 2,400 games moved AT ALL** -- 5 worse, 1 better, sign-test p=0.22, and the 95% CI on the paired
mean delta is **[-0.0003, +0.0037]**, which INCLUDES ZERO. So "+0.0017" was never distinguishable from
noise at that sample size; it is five individual games. A deck-level mean of +0.0083 over 120 games is
by construction ONE game moving ONE turn, and no mean can tell that from chance.

And the per-game decomposition relocates the question entirely:

| deck | gi | unsound | guard | split | shipped |
|---|---|---|---|---|---|
| stompy | 75 | 5 | 6 | 6 | 6 |
| stompy | 110 | 5 | 6 | 6 | 6 |
| hinata | 5 | 6 | 7 | 7 | 7 |
| minotaur | 107 | 4 | 5 | 5 | 5 |
| th | 12 | 5 | 6 | 6 | 6 |
| fivecolour | 46 | 6 | 6 | 6 | **5** |

**Every worse game moves at the GUARD/SPLIT step; the wave costs nothing on any of them, and the one
better game is the wave's doing.**

### The 40,000-game reproduction (seed base 200000, 2026-09-11) — and what it overturned

USER: *"Let's take a look at whether these losses are reproducible on a larger set."* Same design at
**20 decks x 2,000 games** per arm:

| arm | delta vs unsound | 95% CI | moved | better | worse | sign p | units |
|---|---|---|---|---|---|---|---|
| `guard` | +0.0027 | [+0.0020, +0.0033] | 135 | 20 | 115 | <0.001 | 1.558x |
| `split` | +0.0019 | [+0.0014, +0.0025] | 100 | 16 | 84 | <0.001 | 1.290x |
| `shipped` (0.005) | **+0.0008** | [+0.0004, +0.0011] | 43 | 7 | 36 | <0.001 | 0.982x |
| the wave ALONE (shipped vs split) | **-0.0011** | [-0.0016, -0.0007] | 65 | **52** | 13 | <0.001 | **0.761x** |

Three corrections fall out, and all three are about SAMPLE SIZE:

1. **The per-deck concentration was an artifact.** minotaur went +0.0084 -> **exactly 0.0000**, stompy
   +0.0166 -> +0.0025, hinata +0.0083 -> +0.0040, th +0.0083 -> +0.0020; meanwhile creature_giving,
   melira, mirrorwing, antilife and kitty — all flat 0.0000 at 120 games — now show losses. It is a
   thin ~1-in-1,000 rate across MOST decks, not 4 bad decks. No deck survives multiple-comparison
   correction (best raw p = 0.031 over 20 decks).
2. **The cost is HALF what was reported: +0.0008, not +0.0017.** Regression to the mean, exactly as
   that CI (which included zero) warned.
3. **The wave is far stronger than the 120-game run could show**: 52 better / 13 worse at 0.761x units,
   where the small run saw only "better 2 / worse 0" on deck keys.

The claim "the wave costs nothing on the worse games, the fix must be search-side" was drawn from six
games and **it is wrong**: at 40,000 games the wave's share IS the dial, it just runs the other way
(see the re-sizing table above — a SMALLER share means MORE reuse, because the threshold it must clear
is smaller).

## What it cost in ground truth, attributed honestly

Against the tiers' own baselines (`git show <rev>:test/regression_gt.txt`, per GT-moving commit):

| commit | tier | moved | better | worse | net |
|---|---|---|---|---|---|
| `202f938b` split keys + wave | regression | 11 | 3 | 8 | **+0.0688** |
| `31fc60c6` rebaseline | overnight | 40 | 11 | 29 | **+0.0722** |

Both are POSITIVE, i.e. worse in avg-turn terms, and that is the expected and accepted shape: the
baseline being replaced is the UNSOUND engine, which scored better by pruning with false beliefs. The
progression of that cost against the unsound baseline is **+0.1700 -> +0.0990 (split keys) -> +0.0688
(wave)**, so roughly 60% of it has been recovered soundly. Every slower game on both tiers classifies
as churn (or better-than-baseline at 4x/16x).

**A correction to the `31fc60c6` accept note, which is wrong in the GT header:** it says the overnight
tier was "~24 commits stale" and "spans MANY changes, not only the sound-memo arc". It was **10**
commits, and the only ones touching `src/` were `8b2f0efa` and `202f938b` -- both this arc. So that
+0.0722 is this arc's own cost on the overnight tier, NOT other people's drift, and it should not be
read as diluted. Judging the change itself needs the controlled A/Bs above (wave vs the sound
baseline: better 2 / worse 0 / identical 18), not a diff against an unsound predecessor.

## Reading the counters

```
MTG_OF_WAVE_PROBE=1   ->  [of-wave] re_searches= twin_win_available= avail_share= verified= reused=
                          [of-wave] twin_agree= twin_better= twin_worse= agree_share= better_share=
```

`twin_better` is the number that matters when re-validating this on a new deck: it counts nodes where
reuse would have discarded a strictly earlier win. If it ever climbs, the gate needs to be stricter
(a LARGER share), not looser.

`verified` is always 0 in practice and that is expected — a twin's win is essentially never inside
the consulting node's horizon, which is why the old `MTG_MEMO_ORDERFREE_VERIFIED_ONLY` guard was
equivalent to switching order-free reuse off entirely.

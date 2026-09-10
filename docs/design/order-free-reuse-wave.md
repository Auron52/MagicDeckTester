# The order-free reuse wave — recovering split keys' residual without reintroducing the bug

**Status: measured, default OFF pending the suite A/B.** Flags: `MTG_FSL_OF_WAVE` (on/off),
`MTG_OF_WAVE_SHARE` (the gate), plus per-job `of_wave` / `of_wave_share` arms so a sweep runs as ONE
pooled batch. Probe: `MTG_OF_WAVE_PROBE` (counters only, no behaviour).

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

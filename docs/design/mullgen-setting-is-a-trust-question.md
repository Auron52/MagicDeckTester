# The mulligan-generation setting is a TRUST question, not a depth question (2026-08-15)

**User question:** how much do `d2 b3` and `d1 b3` win and lose against `d3 b3`, on a larger test set?
Plus, explicitly: the comparison "could actually be slightly higher R at d2 vs lower R at d3 for the
same cost -- it doesn't have to be straight time savings", and "for profile generation speed is
actually more important than quality... within reason".

Answer, in one line: **`d3 b3` is dominated, `d2`/`d1` are worse than it, and the setting that
actually matters is whether the deck's search can REACH ITS VALUE LEAF.**

## Rule 0 -- this measurement was INVALID before `fix(analyzer): comp scorer ran a VALUE-LESS policy`

`RunScoreCompsMode` never called `AttachValueSidecar`, so every `MTG_SCORE_COMPS` rollout played a
policy no deck with a value model uses. It corrupted both axes at once, and not subtly:

| slivers, units/rollout | value-less (bug) | leaf attached |
|---|---:|---:|
| d3 b3 | 2,967 | 2,510 |
| **d5 b20 (play/trust)** | **18,765** | **1,496** |

i.e. the deck's own play settings looked **6.3x MORE expensive than d3 b3** when they are in fact
**1.9x CHEAPER**. It also manufactured a clean-looking false finding: value-less, `d2 b3` and `d3 b3`
produced BYTE-IDENTICAL labels on slivers and near-identical ones on burn, which read as "the budget
binds before the depth, so d3 buys nothing". With the leaf attached they diverge sharply (rho 0.875 vs
0.984), because d3 reaches the leaf and d2 does not.

**Any prior conclusion drawn from `MTG_SCORE_COMPS` on a deck with a value model is suspect for the
same reason.** The bug was in the shipped scorer, not in the harness.

## What is measured

96 size-7 comps per deck, sampled from the deck's COMMITTED bucket map at the real opening-hand
frequency, scored at R=40 per side. All arms see identical continuations (the rollout seed is a pure
function of `(r, pd)`), so every arm-vs-arm difference is PAIRED -- common random numbers, not a race
between samples.

The reference is each deck's **own shipped play settings**. Labels exist to rank hands for the policy
the deck actually plays, so that -- not a generic "strong" setting -- is the target.

Reported per arm:

* `rmsd_c` -- RMS of the CENTERED residual vs the reference. A mulligan profile is a RANKING and keeps
  are comparisons between hands, so a uniformly-worse labeller yields the IDENTICAL policy. The
  uniform part is harmless; only the centered part reorders hands. (Burn at d0: +0.199 turns of bias,
  of which only 0.106 reorders. This is the quantitative form of the user's "relative symmetry of
  playing worse search on the same hand".)
* `rho` -- Spearman rank correlation with the reference.
* `agree` -- keep/mull agreement at reference quartile thresholds.
* cost -- **deterministic work units per rollout**, not seconds (see below).

### Cost is measured in work units

Wall-clock cannot price a generation setting. It moves with whatever else is on the box -- two
unrelated 12-core runs made an arm look ~3x its true cost during this very sweep -- and it differs per
machine, so a setting derived from seconds cannot survive the cross-machine profile handoff. Work
units are the currency `SearchBudget` is denominated in and are a pure function of
`(deck, seed, depth, budget)`, so they are exact after a handful of rollouts and identical everywhere.

They count SEARCH work only, so **depth 0 measures exactly 0** and would price as free. Total cost is
therefore modelled as `search_units + C`, with the per-rollout non-search baseline `C` pinned from the
d0-vs-anchor wall RATIO (ratios survive a loaded box).

## The result

Cross-deck means over the decks measured, cost relative to `d3 b3`:

| arm | mean rho | mean rmsd_c | mean cost |
|---|---:|---:|---:|
| d0 b3 | 0.852 | 0.258 | 0.139 |
| d1 b3 | 0.966 | 0.058 | 0.691 |
| d2 b3 | 0.971 | 0.048 | 0.913 |
| d3 b3 | 0.989 | 0.036 | 1.000 |
| d5 b3 | 0.997 | 0.022 | 1.169 |
| d3 b20 | 0.998 | 0.016 | 3.280 |
| d5 b20 | 1.000 | 0.004 | 2.001 |

1. **On the original question, d3 beats d2** (+0.018 rho for +9.5% cost), and d1 is worse still. The
   earlier "d2 == d3, indistinguishable" verdict was the bug.
2. **`d3 b3` is DOMINATED.** `d5 b3` buys +0.008 rho for +17% cost, and beats `d3 b20`'s fidelity at
   roughly a third of its price. Depth is not the expensive part; playing out past the leaf is.
3. **d0 is deck-shaped and mostly bad** (mean 0.852) but ranges from 0.994 (burn) to 0.624 (slivers,
   where keep agreement at the median is 0.50 -- a coin flip). It is not an aggro/combo split: knights
   is linear aggro and still fails at 0.855.

### ...but d5's win is a TRUST effect, not a depth effect

Splitting the same arms by whether the deck has a `value_trust_depth` that `d5` actually reaches:

| group | n | d3 b3 rho | d5 b3 rho | delta | d5 b3 cost |
|---|---:|---:|---:|---:|---:|
| **trusted** | 4 | 0.9880 | 0.9980 | **+0.0100** | 1.20x |
| **untrusted** | 2 | 0.9925 | 0.9940 | +0.0015 | 1.25x |

Per deck the gain tracks trust exactly: knights +0.0220, slivers +0.0150, auras +0.0030, burn +0.0000,
against dragonstorm +0.0010 and treasure_hunt +0.0020. **On an untrusted deck, paying 25% more for
depth beyond ~d3 buys nothing.**

## The mechanism, and why the budget is part of the setting

Reaching the value leaf requires depth >= `value_trust_depth` **AND** enough budget to COMPLETE that
depth. The escalation ladder commits the deepest COMPLETED pass, so a starved budget means the deeper
pass is attempted, abandoned, and paid for -- while the committed line is the shallower one.

Two measurements make this concrete, both on slivers:

* `d3 b20` costs **11,551** units while `d5 b20` costs **1,496**. Going DEEPER at the same budget is
  7.7x CHEAPER, because d5 reaches the leaf and d3 has to play the game out.
* `d5 b3` costs **1,602** vs `d5 b20`'s **1,496** -- the starved budget costs MORE, because it
  sometimes fails to complete the depth that would have terminated the line.

So cost is **not monotonic in depth**, and a (depth, budget) pair -- not a depth -- is the setting.
This is the same cliff the value-leaf skill documents as a 1.35-84.8x hazard when a sidecar is missing.

## What this means for the deriver: MEASURE, do not short-circuit on trust

A tempting rule -- *leaf trusted at the shipped play depth -> generate at play settings* -- is what
`valueleaf_table_to_metadata.py` already does, and on two decks it is exactly right: slivers' play
settings are the CHEAPEST arm available (1,496 vs 2,431 for `d3 b3`) and exact by construction, and
knights' cost only +32%.

**But it is not a rule, and the counterexample is not marginal.** `burn` is trusted at its play depth
(V5 <= d6) and its play settings still cost **18,844** units/rollout against **1,253** for `d1 b3` at
rho **0.999** -- 15x the cost for +0.001 rank fidelity. Whether the leaf truncates enough to pay for
the depth is a property of the DECK. So the derivation MEASURES in both branches; trust shapes the
CANDIDATE SET (the trust depth is where the cliff can be, so it must be on the list) but does not
decide the answer.

An earlier draft of this work proposed the opposite rewrite -- cost-primary, ignoring fidelity. That
was based on the corrupted measurement and is **withdrawn**; the floor is real.

`scripts/derive_mullgen_setting.py` implements this. It uses `MTG_SCORE_HANDS=N` (random openers
sampled from the decklist) rather than bucket comps, because the comp path's bucket map is an OUTPUT
of the very generation being configured -- on a new deck it does not exist exactly when the derivation
wants it. Worked results: burn picks `d1 b3` at 0.10x the play cost, treasure_hunt picks `d3 b3` at
0.25x, slivers picks `d3 b3` at 0.60x.

### Caveat: the answer depends on WHICH HANDS are sampled, and neither population is exactly right

The sweep samples the MOST FREQUENT bucket comps; the deriver samples RANDOM OPENERS. They agree on
fidelity (slivers `d3 b3`: 0.984 vs 0.9977) but can disagree on the COST RANKING -- on slivers the
sweep makes `d5 b20` the cheapest arm (1,496 < 2,431) while the deriver makes it the dearest
(5,568 > 3,313). The leaf evidently truncates better on typical hands than on awkward ones, so a
frequency-weighted sample flatters the deep arms.

Neither is the population the GENERATOR actually pays for: it evaluates every DISTINCT composition,
roughly uniformly, not weighted by draw frequency. Frequency weighting is arguably right for QUALITY
(the policy matters most on hands you actually see) and wrong for COST. Treat a cost ratio inside
~2x as unresolved, and prefer the fidelity ordering, which is stable across both populations.

## Open

* `Goblins` has trust V6, so the `d5 b3` arm is BELOW its trust depth and should behave like an
  untrusted deck; the right cheap arm for it is `d6 b3`, which this sweep did not include.
* The measured means carry R=40 noise, so every arm's ceiling is a slight UNDER-estimate. It biases
  all arms alike, so the comparison holds; the absolute rho values are pessimistic.

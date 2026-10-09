# Rollouts only when needed, and estimated ahead of time rather than by spending

**Status: USER DESIGN RULE stated 2026-10-09, partially built, measured once.** The rule is the
user's, in their words. The first increment (`MTG_LAZY_LEAF_BUDGETED`) is built and measured; the
larger scheduling idea is specified here and NOT built.

## The rule

> **"It would be nice if we could just do them only when really needed and estimate ahead of time so
> we don't overrun." … "As a general rule."**

And the complaint that produced it:

> **"It's sort of annoying how we need to do rollouts early just to ensure we don't overrun our
> budget. That seems kind of poor."**

That names a real property of the current ladder. The start gate in `SolveWithLookahead` decides
whether the next pass fits by extrapolating from what the *previous passes actually cost*
(`ratio = c_prev / c_prev2`, `estimate = c_prev * ratio`, `fits = estimate <= gate_alpha *
remaining`). So the engine learns the price of rollouts **by paying it**, and pays it in its most
expensive currency. On a deck whose rollout cost is wildly heterogeneous that calibration is both the
dominant cost and a bad predictor.

## The two halves, and the one that is already half-built

**1. Only when needed.** This is `MTG_LAZY_LEAF`: one leafless pass at full depth *before* the
ladder, so a decision whose win is provable inside the horizon "pays for not a single rollout"; on a
hit the entire leafed ladder is skipped. The code's own note is that the deferred thing is **95%+ of
the work and the thing being paid is not**. It has always been **unlimited-budget only**, because a
probe consumes units and units are a finite budget's currency.

`MTG_LAZY_LEAF_BUDGETED` (default OFF, added 2026-10-09) extends it to a finite budget. The user's
clarification is what makes that legitimate rather than lossy: *"The 'never lossy' idea does not
require us to search everything exhaustively at every budget."* It re-allocates a budget; it refuses
no line, and at an unlimited budget it changes nothing.

**2. Estimate ahead of time.** Not built. But the primitive already exists in the trust-path
override: the avoided escalation is estimated as **`g_trust_R * leaves`** — "R x the probe's leaf
count at the last completed depth. R is the deck's frozen cost-per-leaf". Both factors are available
*without doing any rollouts*: the leaf count falls out of the leafless interior walk, and R is a
per-deck constant. So the leafless probe of part 1 is exactly the thing that makes part 2 possible —
after it, the engine can predict what leafing the ladder would cost before buying a single rollout.
That is the composition that removes the "pay to learn the price" shape.

## The staged schedule the user proposed (NOT built)

> **"Maybe even an approach where we estimate the rollout cost (by running a few?) and then start
> doing more rollouts only when it seems like we are going to need to do them soon and adjust our
> estimate based on that. So you might have a time where you do a few up front and then wait a while
> and then do a larger chunk and then determine you can wait for the rest and only do those when you
> have processed the rest of the depth."**

So: a small calibration sample, then deferral, then a projection-triggered larger chunk that also
re-estimates, then the remainder only if the depth is exhausted and they are still needed. Two
constraints any implementation has to respect, both of which this repo has already paid for once:

* **The schedule must key on UNITS, never on wall clock.** The existing start gate says so in as many
  words — *"Keyed on the running work-unit count, never the clock, so a deeper search makes the same
  skip decision and can never come out worse."* An online estimate derived from measured
  milliseconds would make play depend on machine load and destroy determinism, and
  `units`-vs-`ms` has already produced a 3x misreading on a contended box in this repo.

* **A small up-front sample is a BIASED estimator here, and biased in the dangerous direction.**
  This deck's rollout cost is heavy-tailed: on the killed v3 generation **1.45% of rollouts held ~75%
  of the machine**, worst single rollout 56.8 minutes. The mean of such a distribution is dominated by
  rare values, so "run a few" will almost always come back cheap and the schedule will under-provision
  exactly before the expensive stretch. Mitigations worth building in from the start: carry a
  conservative upper estimate rather than the sample mean, let a chunk that overruns its prediction
  raise the estimate sharply (fast up, slow down), and prefer bounding the *count* of leaf
  evaluations — which is deterministic — over bounding predicted time, which is not.

## Escalation and deferral are complements, not rivals

The user's framing, which the measurement below supports:

> **"with the value-leaf we usually do the escalation approach and that works for a number of our
> decks. Neither approach is strictly better, but escalation is less effective without the value-leaf
> available."**

Escalation needs a **cheap non-committing pass** to climb through, and the value leaf is what makes a
pass cheap. Deferral needs **depth to defer across**. A leaf-less deck searched at depth 1 has
neither: one pass, and that pass pays full rollouts. That is precisely the configuration the killed
kitty v3 generation ran in.

## What was measured (kitty v3, 64 paired size-7 floor cells, `MTG_KEEP_BENCH`, one Release binary)

| depth | `MTG_LAZY_LEAF_BUDGETED` | CPU ms/rollout | units/rollout | win-turn digest |
|---|---|---|---|---|
| 1 | 0 *(the killed run's config)* | 808.54 | 8,639 | `80aca23688f2f183` |
| 1 | 1 | 804.20 | 8,724 | `80aca23688f2f183` |
| 2 | 0 | 860.78 | 9,487 | `80aca23688f2f183` |
| 2 | **1** | **589.44** | **7,979** | `80aca23688f2f183` |

Lazy-leaf telemetry: **hit rate 0.16**, probe cost **3%** of units at d1 and **5%** at d2.

* **d1: a null.** 0.5% on CPU, units slightly up. Nothing to defer across at one pass.
* **d2: 0.685x CPU, 0.84x units — 1.46x.** And 0.729x against the config actually used (d1/OFF).
* Mean win turn 5.28 and the digest identical in all four arms.

## Two honest limits on that result

1. **The sample cannot see the tail that killed the run.** With N=64, `p99` and `max` are the same
   number (7.4 s), while the real generation's worst rollout was 3,405 s. The slowest-1% time share
   reads 13-20% here against ~75% on the full run. So this measures the **median**, and the blowup is
   a **tail** phenomenon. A tail reading needs N in the thousands.
2. **An identical win-turn digest over 64 cells is weak evidence of play-neutrality**, not proof. It
   says these 64 rollouts ended on the same turn. Real adoption needs the regression tiers and the 14
   references, and the lever is default OFF until then.

**So: 1.46x at d2 is real and apparently free, and it is NOT the answer to the blowup.** We needed
roughly an order of magnitude; this is a median-case 1.46x that leaves the tail untouched.

## Where this leaves the ordering question

The user asked whether to run the value leaf first. It is attractive — v2's leaf was adopted as a win
on *both* axes (play −0.00775, t −2.17; cost **0.76x**), and its Phase F is literally the
mulligan-settings measurement that kitty v3 never got (d2 b3 rho 0.9911 / 0.37x, which is why v2's
sidecar carries `mull_gen_depth: 2` while the killed v3 run used d1 from a carried *profile*).

But the user's own doubt is right: *"the value-leaf may also be too expensive… difficult to
generate."* Leaf generation is rollout-fed, its label generation runs **unbudgeted** (where lazy leaf
is already on, so there is no further lever to pull there), and v2's own adoption message records
that phase A had to be closed early with `finish` because *"one trio of games pinned it for hours"* —
on the engine that is 27x cheaper than today's. So leaf-first does not escape the blowup either.

Both generators are rollout-fed. The tail is upstream of every artifact this deck needs.

Related: `docs/design/slow-rollout-tail-and-the-uncharged-greedy-walk.md` (the budget never sees the
subset walk — the diagnosis, deferred by the user),
`docs/design/keepgen-degenerate-cells-are-the-no-puresteel-hands.md` (the tail's shape and the two
explanations that failed), `docs/design/lazy-leaf-corrupts-the-h5-reference.md` (the hazard in the arm
where the lever is already on), `docs/design/mullgen-settings-derived-by-valueleaf.md` (Phase F).
